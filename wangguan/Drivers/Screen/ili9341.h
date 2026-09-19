#ifndef ILI9341_H
#define ILI9341_H

#include "main.h"
#include <stdint.h>

#define ILI9341_WIDTH          320U
#define ILI9341_HEIGHT         240U

#define ILI9341_COLOR_BLACK    0x0000U
#define ILI9341_COLOR_BLUE     0x001FU
#define ILI9341_COLOR_RED      0xF800U
#define ILI9341_COLOR_GREEN    0x07E0U
#define ILI9341_COLOR_CYAN     0x07FFU
#define ILI9341_COLOR_MAGENTA  0xF81FU
#define ILI9341_COLOR_YELLOW   0xFFE0U
#define ILI9341_COLOR_WHITE    0xFFFFU

/**
  * @brief 初始化ILI9341屏幕并清为黑色。
  * @param hspi 屏幕连接的SPI句柄。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ILI9341_Init(SPI_HandleTypeDef *hspi);

/**
  * @brief 设置屏幕背光开关。
  * @param enable 为1时打开背光，为0时关闭背光。
  * @retval 无。
  */
void ILI9341_SetBacklight(uint8_t enable);

/**
  * @brief 使用RGB565颜色填充整个屏幕。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ILI9341_FillScreen(uint16_t color);

/**
  * @brief 使用RGB565颜色填充指定矩形区域。
  * @param x 矩形左上角横坐标。
  * @param y 矩形左上角纵坐标。
  * @param width 矩形宽度。
  * @param height 矩形高度。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ILI9341_FillRect(uint16_t x, uint16_t y,
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
HAL_StatusTypeDef ILI9341_WriteRGB565(uint16_t x, uint16_t y,
                                     uint16_t width, uint16_t height,
                                     const uint8_t *pixel_data);

/**
  * @brief 在指定坐标绘制一个RGB565像素。
  * @param x 像素横坐标。
  * @param y 像素纵坐标。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

#endif
