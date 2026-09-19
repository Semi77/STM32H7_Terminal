#include "usart1_test.h"
#include "Modbus.h"
#include "main.h"

#include "cmsis_os2.h"

#include <stdio.h>

#define USART1_TEST_MAX_FRAME_SIZE       256U
#define USART1_TEST_RECEIVE_BUFFER_SIZE  (USART1_TEST_MAX_FRAME_SIZE + 1U)
#define USART1_TEST_TX_TIMEOUT_MS        100U

typedef enum
{
  USART1_TEST_STATE_RECEIVING = 0,
  USART1_TEST_STATE_FRAME_READY,
  USART1_TEST_STATE_FRAME_TOO_LONG,
  USART1_TEST_STATE_ERROR
} Usart1TestState_t;

static UART_HandleTypeDef *usart1_test_handle;
static uint8_t usart1_test_receive_buffer[USART1_TEST_RECEIVE_BUFFER_SIZE];
static uint8_t usart1_test_response_buffer[40U];
static volatile uint16_t usart1_test_received_length;
static volatile Usart1TestState_t usart1_test_state = USART1_TEST_STATE_ERROR;

static const osThreadAttr_t usart1_test_task_attributes = {
  .name = "usart1TestTask",
  .stack_size = 1024U,
  .priority = (osPriority_t)osPriorityLow,
};

/**
  * @brief 重新启动一次最长257字节的USART1空闲帧接收。
  * @param 无。
  * @retval HAL状态，HAL_OK表示接收启动成功。
  */
static HAL_StatusTypeDef Usart1Test_StartReceive(void)
{
  HAL_StatusTypeDef status;

  usart1_test_received_length = 0U;
  usart1_test_state = USART1_TEST_STATE_RECEIVING;
  status = HAL_UARTEx_ReceiveToIdle_IT(usart1_test_handle,
                                      usart1_test_receive_buffer,
                                      USART1_TEST_RECEIVE_BUFFER_SIZE);
  if (status != HAL_OK)
  {
    usart1_test_state = USART1_TEST_STATE_ERROR;
  }

  return status;
}

/**
  * @brief 处理完整串口帧并通过USART1返回本帧字节数。
  * @param 无。
  * @retval HAL状态，HAL_OK表示处理成功。
  */
static HAL_StatusTypeDef Usart1Test_Process(void)
{
  int response_length;
  HAL_StatusTypeDef status;

  if (usart1_test_state == USART1_TEST_STATE_RECEIVING)
  {
    return HAL_OK;
  }

  if (usart1_test_state == USART1_TEST_STATE_FRAME_READY)
  {
    response_length = snprintf((char *)usart1_test_response_buffer,
                               sizeof(usart1_test_response_buffer),
                               "Received: %u bytes\r\n",
                               (unsigned int)usart1_test_received_length);
    if ((response_length <= 0) ||
        ((size_t)response_length >= sizeof(usart1_test_response_buffer)))
    {
      return HAL_ERROR;
    }

    status = HAL_UART_Transmit(usart1_test_handle,
                               usart1_test_response_buffer,
                               (uint16_t)response_length,
                               USART1_TEST_TX_TIMEOUT_MS);
    if (status != HAL_OK)
    {
      return status;
    }
  }
  else if (usart1_test_state == USART1_TEST_STATE_FRAME_TOO_LONG)
  {
    static const uint8_t overflow_message[] = "Error: frame too long\r\n";

    status = HAL_UART_Transmit(usart1_test_handle,
                               overflow_message,
                               (uint16_t)(sizeof(overflow_message) - 1U),
                               USART1_TEST_TX_TIMEOUT_MS);
    if (status != HAL_OK)
    {
      return status;
    }
  }
  else
  {
    (void)HAL_UART_AbortReceive(usart1_test_handle);
  }

  return Usart1Test_StartReceive();
}

/**
  * @brief 周期处理USART1接收结果并维持下一帧接收。
  * @param argument 未使用的任务参数。
  * @retval 无。
  */
static void Usart1Test_Task(void *argument)
{
  (void)argument;
  g_usart1_started = true;

  for (;;)
  {
    if (Usart1Test_Process() == HAL_OK)
    {
      g_usart1_heartbeat = HAL_GetTick();
    }
    osDelay(1U);
  }
}

/**
  * @brief 启动USART1空闲帧接收和字节数返回测试任务。
  * @param huart 指向USART1的UART句柄。
  * @retval HAL状态，HAL_OK表示测试任务启动成功。
  */
HAL_StatusTypeDef Usart1Test_Start(UART_HandleTypeDef *huart)
{
  osThreadId_t task_handle;

  if ((huart == NULL) || (huart->Instance != USART1))
  {
    return HAL_ERROR;
  }

  usart1_test_handle = huart;
  if (Usart1Test_StartReceive() != HAL_OK)
  {
    return HAL_ERROR;
  }

  task_handle = osThreadNew(Usart1Test_Task, NULL, &usart1_test_task_attributes);
  if (task_handle == NULL)
  {
    (void)HAL_UART_AbortReceive(usart1_test_handle);
    usart1_test_state = USART1_TEST_STATE_ERROR;
    return HAL_ERROR;
  }

  return HAL_OK;
}

/**
  * @brief 保存USART1空闲事件接收到的帧长度。
  * @param huart 产生接收事件的UART句柄。
  * @param Size 当前接收操作已经收到的字节数。
  * @retval 无。
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  HAL_UART_RxEventTypeTypeDef event_type;

  if (huart != usart1_test_handle)
  {
    return;
  }

  event_type = HAL_UARTEx_GetRxEventType(huart);
  if ((event_type == HAL_UART_RXEVENT_IDLE) &&
      (Size <= USART1_TEST_MAX_FRAME_SIZE))
  {
    usart1_test_received_length = Size;
    usart1_test_state = USART1_TEST_STATE_FRAME_READY;
  }
  else if (event_type == HAL_UART_RXEVENT_TC)
  {
    usart1_test_state = USART1_TEST_STATE_FRAME_TOO_LONG;
  }
}

/**
  * @brief 记录USART1接收期间发生的硬件错误。
  * @param huart 发生错误的UART句柄。
  * @retval 无。
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  Modbus_UartError(huart);
  if (huart == usart1_test_handle)
  {
    usart1_test_state = USART1_TEST_STATE_ERROR;
  }
}
