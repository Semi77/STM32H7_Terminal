#ifndef BOOT_TRIAL_H
#define BOOT_TRIAL_H
#include "stm32h7xx_hal.h"
#include <stdbool.h>
/** @brief 将试启动序号token写入RTC寄存器2至5并清除旧确认。 @retval 无。 */
void BootTrial_Arm(uint32_t token);
/** @brief 检查应用确认是否对应token，掉电丢失确认时按未确认处理。 @retval true表示匹配。 */
bool BootTrial_Confirmed(uint32_t token);
/** @brief 清除试启动信箱，保留独立升级请求寄存器0和1。 @retval 无。 */
void BootTrial_Clear(void);
/** @brief healthy表示关键任务健康，连续健康20秒后确认并复位以持久化。 @retval 无。 */
void BootTrial_Poll(bool healthy);
#endif
