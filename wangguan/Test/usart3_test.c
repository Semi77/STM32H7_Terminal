#include "usart3_test.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include "boot_request.h"
#include "main.h"
#include "boot_selftest.h"
#include "selftest_wire.h"
#include "Modbus.h"

static Usart3TestSnapshot_t latest_snapshot;
static bool snapshot_ready;
static UART_HandleTypeDef *status_uart;
static uint8_t status_byte;
static char status_line[24];
static uint8_t status_length;
static bool status_overflow;
static volatile bool status_valid, status_online;
static volatile uint32_t status_tick, ack_sequence;
static volatile bool boot_requested;
static volatile uint32_t selftest_ack;
static const osThreadAttr_t usart3_test_task_attributes = {
  .name = "gatewayUpload",
  .stack_size = 3072U,
  .priority = (osPriority_t)osPriorityLow,
};

/**
  * @brief 在短临界区复制最新模拟采样和上传统计到snapshot。
  * @retval true表示已有采样，false表示尚未初始化或参数为空。
  */
bool Usart3Test_GetSnapshot(Usart3TestSnapshot_t *snapshot)
{
  if (!snapshot) return false;
  taskENTER_CRITICAL();
  bool ready = snapshot_ready;
  *snapshot = latest_snapshot;
  taskEXIT_CRITICAL();
  return ready;
}

/**
  * @brief 原子读取ESP32上传就绪状态，并检查3秒串口心跳超时。
  * @retval true表示可上传，false表示离线或心跳失效。
  */
bool Usart3Test_IsOnline(void)
{
  taskENTER_CRITICAL();
  bool online = status_valid && status_online && (uint32_t)(HAL_GetTick()-status_tick) < 3000U;
  taskEXIT_CRITICAL();
  return online;
}

/**
  * @brief 接收NET心跳或ACK序号，中断内只更新标志并重启接收。
  * @param huart 完成单字节接收的USART3句柄。
  * @retval 无。
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  Modbus_RxComplete(huart);
  if (huart != status_uart) return;
  if (status_byte == '\n') {
    if (!status_overflow && status_length >= 6 && status_line[status_length-1] == '\r') {
      if (status_length == 6 && memcmp(status_line, "NET:", 4) == 0 &&
          (status_line[4] == '0' || status_line[4] == '1')) {
        status_online = status_line[4] == '1';
        status_tick = HAL_GetTick();
        status_valid = true;
      } else if (status_length == 11 && memcmp(status_line, "Bootloader\r", 11) == 0) {
        boot_requested = true;
      } else if (status_length == 16 && memcmp(status_line, "ST_ACK:", 7) == 0) {
        uint32_t value=0;bool valid=true;
        for(unsigned i=7;i<15;++i) {
          int digit=SelfTest_Hex(status_line[i]);
          if(digit<0) {valid=false;break;}
          value=(value<<4)|(uint32_t)digit;
        }
        if(valid) selftest_ack=value;
      } else if (memcmp(status_line, "ACK:", 4) == 0) {
        uint32_t value = 0;
        bool valid = true;
        for (uint32_t i=4; i+1U<status_length; ++i) {
          uint32_t digit = (uint8_t)status_line[i] - (uint32_t)'0';
          if (digit > 9 || value > (UINT32_MAX-digit)/10U) { valid=false; break; }
          value=value*10U+digit;
        }
        if (valid && value) ack_sequence = value;
      }
    }
    status_length=0;
    status_overflow=false;
  } else if (status_length < sizeof(status_line)) {
    status_line[status_length++] = (char)status_byte;
  } else status_overflow=true;
  (void)HAL_UART_Receive_IT(huart, &status_byte, 1);
}

/**
  * @brief 在任务上下文恢复串口错误后的接收，huart为已初始化USART3。
  * @retval 无。
  */
static void ensure_receive(UART_HandleTypeDef *huart)
{
  if (huart->RxState == HAL_UART_STATE_READY) {
    status_length=0;
    status_overflow=false;
    __HAL_UART_CLEAR_OREFLAG(huart);
    (void)HAL_UART_Receive_IT(huart, &status_byte, 1);
  }
}

/**
  * @brief 每秒生成模拟采样，每20毫秒处理联网状态、ACK和Flash补传。
  * @param argument 已初始化的USART3句柄。
  * @retval 无，任务持续运行。
  */
static void Usart3Test_Task(void *argument)
{
  UART_HandleTypeDef *huart=argument;
  bool initialized=GatewayUpload_Init(huart);
  g_usart3_started = true;
  /* 使用Flash持久序号区分重启；Flash不可用时仍允许发送诊断报告。 */
  uint32_t report_run=HAL_GetUIDw0() ^ HAL_GetTick() ^ 0x80000000U;
  if(initialized) (void)OfflineCache_NextSequence(&report_run);
  uint32_t next_sample=HAL_GetTick();
  Usart3TestSnapshot_t snapshot={0};
  for (;;) {
    ensure_receive(huart);
    /* 在上传任务处理复位请求，先保存待确认记录，避免中断内操作Flash。 */
    if (boot_requested) {
      boot_requested = false;
      if (GatewayUpload_PrepareReset() && BootRequest_Set()) {
        const char accepted[] = "BOOT_ACCEPTED\r\n";
        (void)HAL_UART_Transmit(huart, (const uint8_t *)accepted, sizeof(accepted)-1U, 100U);
        NVIC_SystemReset();
      } else {
        const char failed[] = "BOOT_REJECTED\r\n";
        (void)HAL_UART_Transmit(huart, (const uint8_t *)failed, sizeof(failed)-1U, 100U);
      }
    }
    taskENTER_CRITICAL();
    uint32_t ack=ack_sequence;
    ack_sequence=0;
    uint32_t report_ack=selftest_ack;
    selftest_ack=0;
    taskEXIT_CRITICAL();
    GatewayUpload_Process(Usart3Test_IsOnline(), ack);
    BootSelfTest_Process(huart,report_run,report_ack);
    uint32_t now=HAL_GetTick();
    if (initialized && (int32_t)(now-next_sample) >= 0) {
      next_sample=now+1000U;
      GatewaySample sample;
      if (OfflineCache_NextSequence(&sample.sequence)) {
        sample.temperature=25U+sample.sequence%5U;
        sample.humidity=55U+sample.sequence%10U;
        sample.brightness=100U+sample.sequence%100U;
        sample.tick_ms=now;
        GatewayUpload_Submit(&sample);
        snapshot.sequence=sample.sequence;
        snapshot.temperature_c=sample.temperature;
        snapshot.humidity_percent=sample.humidity;
        snapshot.light_lux=sample.brightness;
      }
    }
    snapshot.upload=GatewayUpload_GetStatus();
    snapshot.tx_status=snapshot.upload.tx_status;
    taskENTER_CRITICAL();
    latest_snapshot=snapshot;
    snapshot_ready=true;
    taskEXIT_CRITICAL();
		
		 g_usart3_heartbeat = HAL_GetTick();
		
    (void)osDelay((osKernelGetTickFreq()+49U)/50U);	
  }
}

/**
  * @brief 创建网关上传任务并提前开启状态接收，huart为USART3句柄。
  * @retval HAL_OK表示创建成功，HAL_ERROR表示参数或资源错误。
  */
HAL_StatusTypeDef Usart3Test_Start(UART_HandleTypeDef *huart)
{
  if (!huart || huart->Instance != USART3) return HAL_ERROR;
  status_uart=huart;
  ensure_receive(huart);
  return osThreadNew(Usart3Test_Task, huart, &usart3_test_task_attributes) ? HAL_OK : HAL_ERROR;
}
