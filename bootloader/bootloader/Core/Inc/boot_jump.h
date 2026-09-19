#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <stdbool.h>
#include "memory_layout.h"

#define BOOT_APP_A_START_ADDRESS  APP_FLASH_BASE
#define BOOT_APP_A_END_ADDRESS    APP_FLASH_END

/**
  * @brief 校验应用程序向量表中的初始栈顶地址和复位中断地址。
  * @param app_start 应用程序分区的起始地址。
  * @param app_end 应用程序分区结束后的第一个地址。
  * @retval true表示应用程序入口地址有效，false表示无效。
  */
bool BootJump_IsApplicationValid(uint32_t app_start, uint32_t app_end);

/**
  * @brief 注销Bootloader运行环境并跳转到应用程序复位入口。
  * @param app_start 应用程序向量表的起始地址。
  * @retval 无。
  */
void BootJump_ToApplication(uint32_t app_start);

#ifdef __cplusplus
}
#endif

#endif
