#include "boot_uart.h"
#include "boot_jump.h"
#include "ota_install.h"

#include <stdio.h>
#include <string.h>

#define BOOT_UART_MAX_FRAME_SIZE       256U
#define BOOT_UART_RECEIVE_BUFFER_SIZE  (BOOT_UART_MAX_FRAME_SIZE + 1U)
#define BOOT_UART_TX_TIMEOUT_MS        100U

typedef enum
{
  BOOT_UART_STATE_RECEIVING = 0,
  BOOT_UART_STATE_FRAME_READY,
  BOOT_UART_STATE_FRAME_TOO_LONG,
  BOOT_UART_STATE_ERROR
} BootUartState_t;

static UART_HandleTypeDef *boot_uart_handle;
// 定义缓冲区
static uint8_t boot_uart_receive_buffer[BOOT_UART_RECEIVE_BUFFER_SIZE];
static uint8_t boot_uart_response_buffer[40U];
static const uint8_t boot_uart_app_a_command[] = {'A', 'p', 'p', 'A'};
static volatile uint16_t boot_uart_received_length;
static volatile BootUartState_t boot_uart_state = BOOT_UART_STATE_ERROR;

/**
  * @brief 重新启动一次最长257字节的串口空闲帧接收。
  * @param 无。
  * @retval HAL状态，HAL_OK表示接收启动成功。
  */
static HAL_StatusTypeDef BootUart_StartReceive(void)
{
  HAL_StatusTypeDef status;

  boot_uart_received_length = 0U;
  boot_uart_state = BOOT_UART_STATE_RECEIVING;
  status = HAL_UARTEx_ReceiveToIdle_IT(boot_uart_handle,
                                      boot_uart_receive_buffer,
                                      BOOT_UART_RECEIVE_BUFFER_SIZE);
  if (status != HAL_OK)
  {
    boot_uart_state = BOOT_UART_STATE_ERROR;
  }

  return status;
}

/**
  * @brief 启动Bootloader串口空闲帧接收功能。
  * @param huart 指向用于接收和返回统计结果的UART句柄。
  * @retval HAL状态，HAL_OK表示接收启动成功。
  */
HAL_StatusTypeDef BootUart_Init(UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return HAL_ERROR;
  }

  boot_uart_handle = huart;
  return BootUart_StartReceive();
}

/**
  * @brief 在主循环中处理完整帧并通过串口返回本帧字节数。
  * @param 无。
  * @retval HAL状态，HAL_OK表示处理成功。
  */
HAL_StatusTypeDef BootUart_Process(void)
{
  int response_length;
  HAL_StatusTypeDef status;

  if (boot_uart_state == BOOT_UART_STATE_RECEIVING)
  {
    return HAL_OK;
  }

  if (boot_uart_state == BOOT_UART_STATE_FRAME_READY)
  {
    if ((boot_uart_received_length == sizeof(boot_uart_app_a_command)) &&
        (memcmp(boot_uart_receive_buffer,
                boot_uart_app_a_command,
                sizeof(boot_uart_app_a_command)) == 0))
    {
      if (!OtaInstall_Blocked() && BootJump_IsApplicationValid(BOOT_APP_A_START_ADDRESS,
                                      BOOT_APP_A_END_ADDRESS) && OtaInstall_BeforeBoot())
      {
        static const uint8_t jump_message[] = "Jumping to AppA\r\n";

        status = HAL_UART_Transmit(boot_uart_handle,
                                   jump_message,
                                   (uint16_t)(sizeof(jump_message) - 1U),
                                   BOOT_UART_TX_TIMEOUT_MS);
        if (status != HAL_OK)
        {
          return status;
        }

        BootJump_ToApplication(BOOT_APP_A_START_ADDRESS);
        return HAL_ERROR;
      }
      else
      {
        static const uint8_t invalid_app_message[] = "Error: AppA invalid\r\n";

        status = HAL_UART_Transmit(boot_uart_handle,
                                   invalid_app_message,
                                   (uint16_t)(sizeof(invalid_app_message) - 1U),
                                   BOOT_UART_TX_TIMEOUT_MS);
        if (status != HAL_OK)
        {
          return status;
        }

        return BootUart_StartReceive();
      }
    }

    response_length = snprintf((char *)boot_uart_response_buffer,
                               sizeof(boot_uart_response_buffer),
                               "Received: %u bytes\r\n",
                               (unsigned int)boot_uart_received_length);
    if ((response_length <= 0) ||
        ((size_t)response_length >= sizeof(boot_uart_response_buffer)))
    {
      return HAL_ERROR;
    }

    status = HAL_UART_Transmit(boot_uart_handle,
                               boot_uart_response_buffer,
                               (uint16_t)response_length,
                               BOOT_UART_TX_TIMEOUT_MS);
    if (status != HAL_OK)
    {
      return status;
    }
  }
  else if (boot_uart_state == BOOT_UART_STATE_FRAME_TOO_LONG)
  {
    static const uint8_t overflow_message[] = "Error: frame too long\r\n";

    status = HAL_UART_Transmit(boot_uart_handle,
                               overflow_message,
                               (uint16_t)(sizeof(overflow_message) - 1U),
                               BOOT_UART_TX_TIMEOUT_MS);
    if (status != HAL_OK)
    {
      return status;
    }
  }
  else
  {
    (void)HAL_UART_AbortReceive(boot_uart_handle);
  }

  return BootUart_StartReceive();
}

/**
  * @brief 处理USART空闲事件并保存当前帧的接收结果。
  * @param huart 产生接收事件的UART句柄。
  * @param Size 当前接收操作已经收到的字节数。
  * @retval 无。
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  HAL_UART_RxEventTypeTypeDef event_type;

  if (huart != boot_uart_handle)
  {
    return;
  }

  event_type = HAL_UARTEx_GetRxEventType(huart);
  if ((event_type == HAL_UART_RXEVENT_IDLE) &&
      (Size <= BOOT_UART_MAX_FRAME_SIZE))
  {
    boot_uart_received_length = Size;
    boot_uart_state = BOOT_UART_STATE_FRAME_READY;
  }
  else if (event_type == HAL_UART_RXEVENT_TC)
  {
    boot_uart_state = BOOT_UART_STATE_FRAME_TOO_LONG;
  }
}

/**
  * @brief 记录Bootloader串口接收期间发生的硬件错误。
  * @param huart 发生错误的UART句柄。
  * @retval 无。
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart == boot_uart_handle)
  {
    boot_uart_state = BOOT_UART_STATE_ERROR;
  }
}
