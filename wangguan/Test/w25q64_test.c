#include "w25q64_test.h"

#include "w25q64_qspi.h"

#include <stdio.h>

#define W25Q64_TEST_UART_TIMEOUT_MS 100U

static uint8_t w25q64_manufacturer_id[1];
static uint8_t w25q64_memory_type_id[1];
static uint8_t w25q64_capacity_id[1];
static uint8_t w25q64_id_message[96];

/**
  * @brief 读取W25Q64的三个JEDEC ID并通过指定串口打印。
  * @param huart 指向用于打印ID信息的UART句柄。
  * @retval HAL状态，HAL_OK表示读取和打印均成功。
  */
HAL_StatusTypeDef W25Q64_TestReadID(UART_HandleTypeDef *huart)
{
  HAL_StatusTypeDef status;
  int message_length;

  if ((huart == NULL) || (huart->Instance != USART1))
  {
    return HAL_ERROR;
  }

  status = W25Q64_ReadJedecID(w25q64_manufacturer_id,
                              w25q64_memory_type_id,
                              w25q64_capacity_id);
  if (status != HAL_OK)
  {
    return status;
  }

  message_length = snprintf((char *)w25q64_id_message,
                            sizeof(w25q64_id_message),
                            "Manufacturer ID: 0x%02X\r\n"
                            "Memory Type ID: 0x%02X\r\n"
                            "Capacity ID: 0x%02X\r\n",
                            w25q64_manufacturer_id[0],
                            w25q64_memory_type_id[0],
                            w25q64_capacity_id[0]);
  if ((message_length <= 0) ||
      ((size_t)message_length >= sizeof(w25q64_id_message)))
  {
    return HAL_ERROR;
  }

  return HAL_UART_Transmit(huart,
                           w25q64_id_message,
                           (uint16_t)message_length,
                           W25Q64_TEST_UART_TIMEOUT_MS);
}
