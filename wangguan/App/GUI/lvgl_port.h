#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "main.h"

/**
  * @brief 初始化LVGL与ST7735S并持续运行图形任务。
  * @param hspi 屏幕使用的SPI句柄。
  * @retval 无。
  */
void LVGL_Port_Task(SPI_HandleTypeDef *hspi);

#endif
