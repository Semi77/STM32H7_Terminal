#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Drivers/ModbusRTU/Modbus.c"
#include "../../Drivers/ModbusRTU/modbus_sensor.c"
#include "../modbus_sensor_test.c"

DWT_Type mock_dwt;
CoreDebug_Type mock_debug;
uint32_t SystemCoreClock = 480000000U;
static uint32_t now_ms, next_byte_ms, direction, signals;
static uint8_t *receive_target;
static uint8_t response[48];
static unsigned response_length, response_index, transmit_count;
static bool pending, fail_tx, omit_tc, fail_rx, error_irq, noise, gap;
static UART_HandleTypeDef uart = {USART2, {9600U, 8U, 0U, 1U}};

/** @brief 提供模拟时钟、信号量及任务接口，延时单位为毫秒。 */
uint32_t HAL_GetTick(void) { return now_ms; }
int osKernelGetState(void) { return osKernelRunning; }
uint32_t osKernelGetTickFreq(void) { return 1000U; }
uint32_t osKernelGetTickCount(void) { return now_ms; }
osSemaphoreId_t osSemaphoreNew(uint32_t max, uint32_t initial, const void *attr)
{ (void)max; (void)attr; signals = initial; return &signals; }
osStatus_t osSemaphoreRelease(osSemaphoreId_t id)
{ (void)id; signals = 1U; return osOK; }
osThreadId_t osThreadNew(void (*entry)(void *), void *arg, const osThreadAttr_t *attr)
{ (void)entry; (void)arg; (void)attr; return &uart; }
osStatus_t osDelay(uint32_t ticks) { now_ms += ticks; return osOK; }
osStatus_t osDelayUntil(uint32_t ticks) { now_ms = ticks; return osOK; }

/** @brief 推进模拟中断，覆盖迟到数据、帧内间隔、无应答及硬件错误。 */
osStatus_t osSemaphoreAcquire(osSemaphoreId_t id, uint32_t ticks)
{
    (void)id;
    for (uint32_t i = 0; ; ++i) {
        if (signals) { signals = 0; return osOK; }
        if (i == ticks) return -1;
        ++now_ms;
        mock_dwt.CYCCNT = now_ms * 480000U;
        if (noise && receive_target) {
            *receive_target = 0xAAU;
            receive_target = NULL;
            Modbus_RxComplete(&uart);
        } else if (pending && (int32_t)(now_ms - next_byte_ms) >= 0) {
            if (error_irq) {
                pending = false;
                Modbus_UartError(&uart);
            } else if (response_index < response_length && receive_target) {
                assert(direction == GPIO_PIN_RESET);
                *receive_target = response[response_index++];
                receive_target = NULL;
                Modbus_RxComplete(&uart);
                next_byte_ms = now_ms + ((gap && response_index == 3U) ? 4U : 1U);
            }
        }
    }
}

/** @brief 模拟方向引脚和UART中断接口，发送开始及结束时校验方向。 */
void HAL_GPIO_WritePin(void *port, uint16_t pin, uint32_t state)
{ (void)port; (void)pin; direction = state; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u, uint8_t *data, uint16_t count)
{ (void)u; assert(count == 1U); if (fail_rx) return HAL_ERROR; receive_target = data; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{ (void)u; receive_target = NULL; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *u)
{ pending = false; return HAL_UART_AbortReceive(u); }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u, const uint8_t *data, uint16_t count)
{
    assert(u == &uart && direction == GPIO_PIN_SET && count == 8U);
    assert(data[1] == 4U && data[2] == 0U && data[3] == 0U && data[5] == 2U);
    assert(data[6] == 0x71U && data[7] == (data[0] == 1U ? 0xCBU : 0xF8U));
    ++transmit_count;
    if (fail_tx) return HAL_ERROR;
    if (!omit_tc) {
        HAL_UART_TxCpltCallback(u);
        assert(direction == GPIO_PIN_RESET);
        pending = true;
        next_byte_ms = now_ms + 2U;
    }
    return HAL_OK;
}

/** @brief 为模拟响应补充CRC，并保留测试代码自行破坏报文的能力。 */
static void finish_response(unsigned data_length)
{
    uint16_t crc = Modbus_Crc(response, (uint16_t)data_length);
    response[data_length] = (uint8_t)crc;
    response[data_length + 1U] = (uint8_t)(crc >> 8);
    response_length = data_length + 2U;
}

/** @brief 配置下一次模拟事务的站号、温湿度和正常应答。 */
static void prepare(uint8_t address, uint16_t temperature, uint16_t humidity)
{
    fail_tx = omit_tc = fail_rx = error_irq = noise = gap = false;
    response_index = 0;
    response[0] = address; response[1] = 4U; response[2] = 4U;
    response[3] = (uint8_t)(temperature >> 8); response[4] = (uint8_t)temperature;
    response[5] = (uint8_t)(humidity >> 8); response[6] = (uint8_t)humidity;
    finish_response(7U);
}

/** @brief 验证错误不覆盖调用者旧数据，且事务退出后总线和接收资源已释放。 */
static void expect_error(ModbusStatus expected)
{
    int16_t temperature = -123;
    uint16_t humidity = 456U;
    uint32_t start = now_ms;
    assert(ModbusSensor_Read(1U, &temperature, &humidity) == expected);
    assert(temperature == -123 && humidity == 456U);
    assert(!bus_busy && !receive_target && !pending && direction == GPIO_PIN_RESET);
    assert((uint32_t)(now_ms - start) <= 200U);
}

/** @brief 执行真实驱动的正常、异常、恢复及快照过期测试。 */
int main(void)
{
    assert(Modbus_Init(&uart));
    const uint8_t known[] = {1,4,0,0,0,2};
    assert(Modbus_Crc(known, sizeof(known)) == 0xCB71U);
    int16_t temperature;
    uint16_t humidity;
    prepare(1, 250, 500);
    assert(ModbusSensor_Read(1, &temperature, &humidity) == MODBUS_OK);
    assert(temperature == 250 && humidity == 500);
    ModbusDiagnostics diagnostics;
    assert(Modbus_GetDiagnostics(1, &diagnostics));
    assert(diagnostics.tx_started && diagnostics.tx_complete && diagnostics.rx_bytes == 9U);
    prepare(2, 10250, 0);
    assert(ModbusSensor_Read(2, &temperature, &humidity) == MODBUS_OK);
    assert(temperature == -250 && humidity == 0);
    prepare(1, 10400, 1000);
    assert(ModbusSensor_Read(1, &temperature, &humidity) == MODBUS_OK && temperature == -400);
    prepare(1, 1250, 1000);
    assert(ModbusSensor_Read(1, &temperature, &humidity) == MODBUS_OK && temperature == 1250);
    prepare(1, 0, 0);
    assert(ModbusSensor_Read(1, &temperature, &humidity) == MODBUS_OK && temperature == 0);
    prepare(1, 250, 500); response[8] ^= 1U; expect_error(MODBUS_CRC_ERROR);
    prepare(2, 250, 500); expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 250, 500); response[1] = 3; finish_response(7); expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 250, 500); response[2] = 2; finish_response(7); expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 250, 500); response[1] = 0x84; response[2] = 2; finish_response(3); expect_error(MODBUS_EXCEPTION);
    prepare(1, 250, 500); response_length = 4; expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 250, 500); response_length = 0; expect_error(MODBUS_TIMEOUT);
    assert(Modbus_GetDiagnostics(1, &diagnostics));
    assert(diagnostics.tx_started && diagnostics.tx_complete && diagnostics.rx_bytes == 0U);
    prepare(1, 250, 500); response_length = sizeof(response); expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 250, 500); gap = true; expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 1251, 500); expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 10401, 500); expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 250, 1001); expect_error(MODBUS_FRAME_ERROR);
    prepare(1, 250, 500); fail_tx = true; expect_error(MODBUS_UART_ERROR);
    prepare(1, 250, 500); fail_rx = true; expect_error(MODBUS_UART_ERROR);
    prepare(1, 250, 500); error_irq = true; expect_error(MODBUS_UART_ERROR);
    prepare(1, 250, 500); omit_tc = true; expect_error(MODBUS_TIMEOUT);
    assert(Modbus_GetDiagnostics(1, &diagnostics));
    assert(diagnostics.tx_started && !diagnostics.tx_complete && diagnostics.rx_bytes == 0U);
    prepare(1, 250, 500); noise = true;
    unsigned previous_transmits = transmit_count;
    expect_error(MODBUS_TIMEOUT);
    assert(transmit_count == previous_transmits);
    prepare(1, 250, 500); now_ms = UINT32_MAX - 10U;
    assert(ModbusSensor_Read(1, &temperature, &humidity) == MODBUS_OK);
    assert(temperature == 250 && humidity == 500);
    assert(ModbusSensor_Read(0, &temperature, &humidity) == MODBUS_ARGUMENT_ERROR);
    assert(ModbusSensor_Read(1, NULL, &humidity) == MODBUS_ARGUMENT_ERROR);
    bus_busy = true;
    assert(ModbusSensor_Read(1, &temperature, &humidity) == MODBUS_BUSY);
    bus_busy = false;
    assert(ModbusSensor_Start(&uart));
    ModbusSensorSample snapshot;
    assert(ModbusSensor_GetSnapshot(0, &snapshot) && !snapshot.valid && snapshot.address == 1);
    samples[1].valid = true; samples[1].last_success_ms = now_ms;
    samples[1].temperature_x10 = -250;
    assert(ModbusSensor_GetSnapshot(1, &snapshot) && snapshot.valid && snapshot.address == 2);
    now_ms += 3000U;
    assert(ModbusSensor_GetSnapshot(1, &snapshot) && !snapshot.valid && snapshot.temperature_x10 == -250);
    assert(!ModbusSensor_GetSnapshot(2, &snapshot));
    puts("PASS: requests, values, CRC, exception, framing, range, timeout, UART errors, direction, recovery, rollover, snapshots.");
    return 0;
}
