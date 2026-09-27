#ifndef BOOT_PAGE_H
#define BOOT_PAGE_H
#include <stdint.h>

/**
  * @brief 在引导页进度条下方显示英文阶段文字。
  * @param text 要显示的零结尾英文字符串。
  * @param color 文字的RGB565颜色。
  * @retval 无。
  */
void BootPage_DrawStatus(const char *text, uint16_t color);

/**
  * @brief 无请求时启动有效Bank2应用，否则进入裸机引导页面并处理串口命令。
  * @retval 无，函数不返回。
  */
void BootPage_Run(void);
#endif
