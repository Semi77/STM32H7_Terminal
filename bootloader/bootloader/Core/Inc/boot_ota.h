#ifndef BOOT_OTA_H
#define BOOT_OTA_H
#include "stm32h7xx_hal.h"
#include <stdbool.h>
/**
  * @brief 初始化下载使用的QSPI外部Flash，不擦写内容。
  * @retval true表示W25Q64可用。
  */
bool BootOta_Init(void);
/**
  * @brief 处理UART收到的字节；uart为串口句柄，byte为已收到字节。
  * @retval true表示该字节被二进制协议占用。
  */
bool BootOta_Byte(UART_HandleTypeDef *uart, uint8_t byte);
/**
  * @brief 释放60秒无活动的未完成下载会话；已完成下载保留。
  * @retval 无。
  */
void BootOta_Poll(void);
/** @brief 串口接收溢出后清除残帧。 @retval 无。 */
void BootOta_ResetReceiver(void);
/**
  * @brief 返回是否正在接收或校验，用于阻止其他串口提前跳转。
  * @retval true表示下载会话未结束。
  */
bool BootOta_Busy(void);
#endif
