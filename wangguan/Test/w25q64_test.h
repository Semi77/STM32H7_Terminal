#ifndef W25Q64_TEST_H
#define W25Q64_TEST_H

#include "stm32h7xx_hal.h"

/**
  * @brief 读取W25Q64的三个JEDEC ID并通过指定串口打印。
  * @param huart 指向用于打印ID信息的UART句柄。
  * @retval HAL状态，HAL_OK表示读取和打印均成功。
  */
HAL_StatusTypeDef W25Q64_TestReadID(UART_HandleTypeDef *huart);

#endif
