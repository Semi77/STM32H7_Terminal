#ifndef BOOT_REQUEST_H
#define BOOT_REQUEST_H
#include "stm32h7xx_hal.h"
#include <stdbool.h>

/**
  * @brief 使用备份寄存器0和1保存单次引导请求，供软件复位后读取。
  * @retval true表示请求已写入并读回一致。
  */
bool BootRequest_Set(void);

/**
  * @brief 读取并清除单次引导请求，不修改外部Flash参数或备份域时钟源。
  * @retval true表示存在有效请求。
  */
bool BootRequest_Take(void);
#endif
