#ifndef USART1_TEST_H
#define USART1_TEST_H

#include "stm32h7xx_hal.h"

/**
  * @brief 启动USART1空闲帧接收和字节数返回测试任务。
  * @param huart 指向USART1的UART句柄。
  * @retval HAL状态，HAL_OK表示测试任务启动成功。
  */
HAL_StatusTypeDef Usart1Test_Start(UART_HandleTypeDef *huart);

#endif
