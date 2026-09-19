#ifndef BOOT_SELFTEST_H
#define BOOT_SELFTEST_H
#include "stm32h7xx_hal.h"
#include <stdint.h>
/** @brief 创建独立SD自检任务，创建失败也生成可上报的失败记录。 @retval 无。 */
void BootSelfTest_Start(void);
/** @brief 上传任务调用，uart为共享串口，run为本次报告编号，ack为ESP确认的CRC。 @retval 无。 */
void BootSelfTest_Process(UART_HandleTypeDef *uart,uint32_t run,uint32_t ack);
#endif
