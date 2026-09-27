#ifndef ST7735S_H
#define ST7735S_H

#include "main.h"
#include <stdint.h>

#define ST7735S_WIDTH          128U
#define ST7735S_HEIGHT         160U
/* 128×160模组的显存起点，出现整体偏移时按模组规格调整。 */
#define ST7735S_X_OFFSET       0U
#define ST7735S_Y_OFFSET       0U

#define ST7735S_COLOR_BLACK    0x0000U
#define ST7735S_COLOR_BLUE     0x001FU
#define ST7735S_COLOR_RED      0xF800U
#define ST7735S_COLOR_GREEN    0x07E0U
#define ST7735S_COLOR_CYAN     0x07FFU
#define ST7735S_COLOR_MAGENTA  0xF81FU
#define ST7735S_COLOR_YELLOW   0xFFE0U
#define ST7735S_COLOR_WHITE    0xFFFFU

/**
  * @brief 初始化ST7735S屏幕并清为黑色。
  * @param hspi 屏幕连接的SPI句柄。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ST7735S_Init(SPI_HandleTypeDef *hspi);

/**
  * @brief 设置屏幕背光开关。
  * @param enable 为1时打开背光，为0时关闭背光。
  * @retval 无。
  */
void ST7735S_SetBacklight(uint8_t enable);

/**
  * @brief 上位机有效指令到达后将背光恢复全亮并重新开始五秒计时。
  * @retval 无，必须从图形任务调用。
  */
void ST7735S_BacklightActivity(void);

/**
  * @brief 五秒没有新的有效指令时将背光降到百分之十亮度。
  * @retval 无，必须从图形任务周期调用。
  */
void ST7735S_BacklightPoll(void);

/**
  * @brief 使用RGB565颜色填充整个屏幕。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ST7735S_FillScreen(uint16_t color);

/**
  * @brief 使用RGB565颜色填充指定矩形区域。
  * @param x 矩形左上角横坐标。
  * @param y 矩形左上角纵坐标。
  * @param width 矩形宽度。
  * @param height 矩形高度。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ST7735S_FillRect(uint16_t x, uint16_t y,
                                  uint16_t width, uint16_t height,
                                  uint16_t color);

/**
  * @brief 将高字节在前的RGB565像素数据写入指定屏幕区域。
  * @param x 区域左上角横坐标。
  * @param y 区域左上角纵坐标。
  * @param width 区域宽度。
  * @param height 区域高度。
  * @param pixel_data 待写入的RGB565字节流地址。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ST7735S_WriteRGB565(uint16_t x, uint16_t y,
                                     uint16_t width, uint16_t height,
                                     const uint8_t *pixel_data);

/**
  * @brief 在指定坐标绘制一个RGB565像素。
  * @param x 像素横坐标。
  * @param y 像素纵坐标。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ST7735S_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

#endif
