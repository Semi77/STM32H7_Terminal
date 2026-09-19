#ifndef BOOT_UART_H
#define BOOT_UART_H

#include "stm32h7xx_hal.h"

/**
  * @brief 启动Bootloader串口空闲帧接收功能。
  * @param huart 指向用于接收和返回统计结果的UART句柄。
  * @retval HAL状态，HAL_OK表示接收启动成功。
  */
HAL_StatusTypeDef BootUart_Init(UART_HandleTypeDef *huart);

/**
  * @brief 在主循环中处理完整帧并通过串口返回本帧字节数。
  * @param 无。
  * @retval HAL状态，HAL_OK表示处理成功。
  */
HAL_StatusTypeDef BootUart_Process(void);

#endif
