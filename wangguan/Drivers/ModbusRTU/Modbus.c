#include "Modbus.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define MODBUS_MAX_REGISTERS 16U
#define MODBUS_QUIET_MS 6U

static UART_HandleTypeDef *bus_uart;
static SemaphoreHandle_t bus_event;
static volatile bool bus_busy, tx_complete, uart_failed, frame_failed;
static volatile uint16_t rx_length;
static volatile uint16_t rx_seen;
static volatile uint32_t last_rx_ms, last_rx_cycles;
static uint32_t max_byte_gap_cycles;
static uint8_t rx_byte;
static uint8_t rx_frame[5U + 2U * MODBUS_MAX_REGISTERS];
static ModbusDiagnostics last_diagnostics[2];
static bool diagnostics_ready[2];

/**
  * @brief 切换MAX485的DE和低电平有效的RE，发送时关闭接收、接收时释放总线。
  * @param transmit true为发送方向，false为接收方向。
  * @retval 无。
  */
static void Modbus_SetTransmit(bool transmit)
{
    if (transmit) {
        HAL_GPIO_WritePin(Modbus_RE_GPIO_Port, Modbus_RE_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(Modbus_DE_GPIO_Port, Modbus_DE_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(Modbus_DE_GPIO_Port, Modbus_DE_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(Modbus_RE_GPIO_Port, Modbus_RE_Pin, GPIO_PIN_RESET);
    }
}

/**
  * @brief 复制站号1或2最近一次事务的发送与接收状态。
  * @param address 站号；diagnostics为非空输出地址。
  * @retval true表示存在记录。
  */
bool Modbus_GetDiagnostics(uint8_t address, ModbusDiagnostics *diagnostics)
{
    if (address < 1U || address > 2U || !diagnostics) return false;
    taskENTER_CRITICAL();
    bool ready = diagnostics_ready[address - 1U];
    if (ready) *diagnostics = last_diagnostics[address - 1U];
    taskEXIT_CRITICAL();
    return ready;
}

/**
  * @brief 计算Modbus CRC16，返回值发送时先低字节后高字节。
  * @param data 字节数组；length为参与校验的字节数。
  * @retval 16位CRC校验值。
  */
static uint16_t Modbus_Crc(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U)
                             : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

/**
  * @brief 校验完整04响应，包括异常响应，校验通过后才写入寄存器数组。
  * @param address 目标站号；count为请求寄存器数；values为输出数组。
  * @retval CRC、帧格式、从站异常或成功状态。
  */
static ModbusStatus Modbus_Parse(uint8_t address, uint16_t count, uint16_t *values)
{
    uint16_t length = rx_length;
    if (frame_failed || length < 5U) return MODBUS_FRAME_ERROR;
    uint16_t crc = Modbus_Crc(rx_frame, (uint16_t)(length - 2U));
    if (rx_frame[length - 2U] != (uint8_t)crc ||
        rx_frame[length - 1U] != (uint8_t)(crc >> 8)) return MODBUS_CRC_ERROR;
    if (rx_frame[0] != address) return MODBUS_FRAME_ERROR;
    if (rx_frame[1] == 0x84U && length == 5U) return MODBUS_EXCEPTION;
    if (rx_frame[1] != 0x04U || rx_frame[2] != count * 2U ||
        length != 5U + count * 2U) return MODBUS_FRAME_ERROR;
    for (uint16_t i = 0U; i < count; ++i) {
        values[i] = (uint16_t)(((uint16_t)rx_frame[3U + i * 2U] << 8) |
                              rx_frame[4U + i * 2U]);
    }
    return MODBUS_OK;
}

/**
  * @brief 初始化固定9600、8N1总线及DWT帧内间隔计时，不重置已有周期计数。
  * @param uart 已初始化的USART2句柄。
  * @retval true表示初始化成功。
  */
bool Modbus_Init(UART_HandleTypeDef *uart)
{
    if (!uart || uart->Instance != USART2 || uart->Init.BaudRate != 9600U ||
        uart->Init.WordLength != UART_WORDLENGTH_8B ||
        uart->Init.Parity != UART_PARITY_NONE || uart->Init.StopBits != UART_STOPBITS_1 ||
        SystemCoreClock < 1000000U) return false;
    if (bus_uart) return bus_uart == uart;
    bus_event = xSemaphoreCreateBinary();
    if (!bus_event) return false;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55UL;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    /* 相邻字节完成时刻之差包含一个字符，加1.5字符空闲共25个位时间。 */
    max_byte_gap_cycles = (SystemCoreClock / 9600U) * 25U;
    bus_uart = uart;
    Modbus_SetTransmit(false);
    return true;
}

/**
  * @brief 收集响应并持续接收，利用DWT拒绝帧内过长间隔。
  * @param uart 产生单字节接收完成事件的串口。
  * @retval 无。
  */
void Modbus_RxComplete(UART_HandleTypeDef *uart)
{
    if (!bus_uart || uart != bus_uart || !bus_busy) return;
    uint32_t cycles = DWT->CYCCNT;
    last_rx_ms = HAL_GetTick();
    ++rx_seen;
    if (tx_complete) {
        if (rx_length && (uint32_t)(cycles - last_rx_cycles) > max_byte_gap_cycles)
            frame_failed = true;
        if (rx_length < sizeof(rx_frame)) rx_frame[rx_length++] = rx_byte;
        else frame_failed = true;
    }
    last_rx_cycles = cycles;
    if (HAL_UART_Receive_IT(uart, &rx_byte, 1U) != HAL_OK) uart_failed = true;
    BaseType_t wake = pdFALSE;
    (void)xSemaphoreGiveFromISR(bus_event, &wake);
    portYIELD_FROM_ISR(wake);
}

/**
  * @brief UART的TC中断确认停止位发送完毕后立即释放RS485总线。
  * @param uart 产生发送完成事件的串口。
  * @retval 无。
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (!bus_uart || uart != bus_uart || !bus_busy) return;
    tx_complete = true;
    Modbus_SetTransmit(false);
    BaseType_t wake = pdFALSE;
    (void)xSemaphoreGiveFromISR(bus_event, &wake);
    portYIELD_FROM_ISR(wake);
}

/**
  * @brief 记录硬件错误，清理及重新接收由任务上下文完成。
  * @param uart 产生错误的串口。
  * @retval 无。
  */
void Modbus_UartError(UART_HandleTypeDef *uart)
{
    if (!bus_uart || uart != bus_uart || !bus_busy) return;
    uart_failed = true;
    BaseType_t wake = pdFALSE;
    (void)xSemaphoreGiveFromISR(bus_event, &wake);
    portYIELD_FROM_ISR(wake);
}

/**
  * @brief 执行一次输入寄存器事务，等待期间通过信号量让出CPU。
  * @param address 站号；start为起始地址；count为数量；values为输出；timeout_ms为总超时。
  * @retval 通信结果，所有出口均恢复接收方向。
  */
ModbusStatus Modbus_ReadInputRegisters(uint8_t address, uint16_t start,
                                      uint16_t count, uint16_t *values,
                                      uint32_t timeout_ms)
{
    if (!bus_uart || !values || !address || address > 247U || !count ||
        count > MODBUS_MAX_REGISTERS || (uint32_t)start + count > 65536U ||
        timeout_ms < 20U || timeout_ms > 1000U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || __get_IPSR() != 0U)
        return MODBUS_ARGUMENT_ERROR;

    taskENTER_CRITICAL();
    bool occupied = bus_busy;
    if (!occupied) bus_busy = true;
    taskEXIT_CRITICAL();
    if (occupied) return MODBUS_BUSY;

    uint8_t request[8] = {address, 0x04U, (uint8_t)(start >> 8), (uint8_t)start,
                          (uint8_t)(count >> 8), (uint8_t)count, 0U, 0U};
    uint16_t crc = Modbus_Crc(request, 6U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8);
    ModbusStatus result = MODBUS_TIMEOUT;
    bool sent = false;
    uint32_t begin = HAL_GetTick();
    tx_complete = uart_failed = frame_failed = false;
    rx_length = 0U;
    rx_seen = 0U;
    last_rx_ms = begin;
    while (xSemaphoreTake(bus_event, 0U) == pdTRUE) {}
    (void)HAL_UART_Abort(bus_uart);
    __HAL_UART_CLEAR_FLAG(bus_uart, UART_CLEAR_OREF | UART_CLEAR_NEF |
                                   UART_CLEAR_FEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(bus_uart, UART_RXDATA_FLUSH_REQUEST);
    Modbus_SetTransmit(false);
    if (HAL_UART_Receive_IT(bus_uart, &rx_byte, 1U) != HAL_OK) uart_failed = true;

    while ((uint32_t)(HAL_GetTick() - begin) < timeout_ms) {
        if (uart_failed) { result = MODBUS_UART_ERROR; break; }
        /* 收发前后均等待至少6毫秒静默，保守覆盖9600、8N1的3.5字符间隔。 */
        if ((uint32_t)(HAL_GetTick() - last_rx_ms) >= MODBUS_QUIET_MS) {
            if (!sent) {
                Modbus_SetTransmit(true);
                if (HAL_UART_Transmit_IT(bus_uart, request, sizeof(request)) != HAL_OK) {
                    result = MODBUS_UART_ERROR;
                    break;
                }
                sent = true;
            } else if (tx_complete && rx_length) {
                /* 停止中断接收后解析，避免解析时缓冲区继续被修改。 */
                (void)HAL_UART_AbortReceive(bus_uart);
                result = Modbus_Parse(address, count, values);
                break;
            }
        }
        uint32_t elapsed = HAL_GetTick() - begin;
        if (elapsed >= timeout_ms) break;
        uint32_t wait_ms = timeout_ms - elapsed;
        if (wait_ms > MODBUS_QUIET_MS) wait_ms = MODBUS_QUIET_MS;
        TickType_t ticks = (TickType_t)(((uint64_t)wait_ms * configTICK_RATE_HZ + 999U) / 1000U);
        (void)xSemaphoreTake(bus_event, ticks ? ticks : 1U);
    }

    /* 超时也中止发送，确保异步传输不再引用栈上的请求数组。 */
    (void)HAL_UART_Abort(bus_uart);
    Modbus_SetTransmit(false);
    taskENTER_CRITICAL();
    if (address <= 2U) {
        last_diagnostics[address - 1U].tx_started = sent;
        last_diagnostics[address - 1U].tx_complete = tx_complete;
        last_diagnostics[address - 1U].rx_bytes = rx_seen;
        diagnostics_ready[address - 1U] = true;
    }
    bus_busy = false;
    taskEXIT_CRITICAL();
    return result;
}
