#include "ili9341.h"

#define ILI9341_SPI_TIMEOUT_MS  1000U
#define ILI9341_CMD_COLUMN_ADDR 0x2AU
#define ILI9341_CMD_PAGE_ADDR   0x2BU
#define ILI9341_CMD_MEMORY_WRITE 0x2CU

typedef struct
{
  uint8_t command;
  uint8_t length;
  uint8_t data[15];
  uint16_t delay_ms;
} ILI9341_InitCommandTypeDef;

static SPI_HandleTypeDef *s_ili9341_spi;
static uint8_t s_ili9341_line_buffer[ILI9341_WIDTH * 2U];

static const ILI9341_InitCommandTypeDef s_ili9341_init_commands[] =
{
  {0x11U, 0U, {0U}, 120U},
  {0xCFU, 3U, {0x00U, 0xC1U, 0x30U}, 0U},
  {0xEDU, 4U, {0x64U, 0x03U, 0x12U, 0x81U}, 0U},
  {0xE8U, 3U, {0x85U, 0x00U, 0x79U}, 0U},
  {0xCBU, 5U, {0x39U, 0x2CU, 0x00U, 0x34U, 0x02U}, 0U},
  {0xF7U, 1U, {0x20U}, 0U},
  {0xEAU, 2U, {0x00U, 0x00U}, 0U},
  {0xC0U, 1U, {0x1DU}, 0U},
  {0xC1U, 1U, {0x12U}, 0U},
  {0xC5U, 2U, {0x33U, 0x3FU}, 0U},
  {0xC7U, 1U, {0x92U}, 0U},
  {0x3AU, 1U, {0x55U}, 0U},
  {0x36U, 1U, {0x68U}, 0U},
  {0xB1U, 2U, {0x00U, 0x12U}, 0U},
  {0xB6U, 2U, {0x0AU, 0xA2U}, 0U},
  {0x44U, 1U, {0x02U}, 0U},
  {0xF2U, 1U, {0x00U}, 0U},
  {0x26U, 1U, {0x01U}, 0U},
  {0xE0U, 15U, {0x0FU, 0x22U, 0x1CU, 0x1BU, 0x08U,
                 0x0FU, 0x48U, 0xB8U, 0x34U, 0x05U,
                 0x0CU, 0x09U, 0x0FU, 0x07U, 0x00U}, 0U},
  {0xE1U, 15U, {0x00U, 0x23U, 0x24U, 0x07U, 0x10U,
                 0x07U, 0x38U, 0x47U, 0x4BU, 0x0AU,
                 0x13U, 0x06U, 0x30U, 0x38U, 0x0FU}, 0U},
  {0x29U, 0U, {0U}, 20U}
};

/**
  * @brief 通过SPI发送一段字节数据。
  * @param data 待发送数据地址。
  * @param length 待发送字节数。
  * @retval HAL状态。
  */
static HAL_StatusTypeDef ILI9341_Transmit(const uint8_t *data, uint16_t length)
{
  if ((s_ili9341_spi == NULL) || (data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  return HAL_SPI_Transmit(s_ili9341_spi, (uint8_t *)data,
                          length, ILI9341_SPI_TIMEOUT_MS);
}

/**
  * @brief 向ILI9341发送一个命令及其参数。
  * @param command 命令字节。
  * @param data 命令参数地址，无参数时传入NULL。
  * @param length 命令参数字节数。
  * @retval HAL状态。
  */
static HAL_StatusTypeDef ILI9341_WriteCommand(uint8_t command,
                                              const uint8_t *data,
                                              uint16_t length)
{
  HAL_StatusTypeDef status;

  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Screen_DC_GPIO_Port, Screen_DC_Pin, GPIO_PIN_RESET);

  status = ILI9341_Transmit(&command, 1U);
  if ((status == HAL_OK) && (length > 0U))
  {
    HAL_GPIO_WritePin(Screen_DC_GPIO_Port, Screen_DC_Pin, GPIO_PIN_SET);
    status = ILI9341_Transmit(data, length);
  }

  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_SET);
  return status;
}

/**
  * @brief 设置ILI9341后续显存写入的坐标窗口。
  * @param x_start 起始横坐标。
  * @param y_start 起始纵坐标。
  * @param x_end 结束横坐标且包含该坐标。
  * @param y_end 结束纵坐标且包含该坐标。
  * @retval HAL状态。
  */
static HAL_StatusTypeDef ILI9341_SetAddressWindow(uint16_t x_start,
                                                  uint16_t y_start,
                                                  uint16_t x_end,
                                                  uint16_t y_end)
{
  uint8_t address[4];
  HAL_StatusTypeDef status;

  address[0] = (uint8_t)(x_start >> 8U);
  address[1] = (uint8_t)x_start;
  address[2] = (uint8_t)(x_end >> 8U);
  address[3] = (uint8_t)x_end;
  status = ILI9341_WriteCommand(ILI9341_CMD_COLUMN_ADDR, address, sizeof(address));
  if (status != HAL_OK)
  {
    return status;
  }

  address[0] = (uint8_t)(y_start >> 8U);
  address[1] = (uint8_t)y_start;
  address[2] = (uint8_t)(y_end >> 8U);
  address[3] = (uint8_t)y_end;
  status = ILI9341_WriteCommand(ILI9341_CMD_PAGE_ADDR, address, sizeof(address));
  if (status != HAL_OK)
  {
    return status;
  }

  return ILI9341_WriteCommand(ILI9341_CMD_MEMORY_WRITE, NULL, 0U);
}

/**
  * @brief 初始化ILI9341屏幕并清为黑色。
  * @param hspi 屏幕连接的SPI句柄。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ILI9341_Init(SPI_HandleTypeDef *hspi)
{
  HAL_StatusTypeDef status;
  uint32_t index;

  if (hspi == NULL)
  {
    return HAL_ERROR;
  }

  s_ili9341_spi = hspi;
  ILI9341_SetBacklight(0U);
  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(Screen_DC_GPIO_Port, Screen_DC_Pin, GPIO_PIN_RESET);

  HAL_GPIO_WritePin(Screen_RES_GPIO_Port, Screen_RES_Pin, GPIO_PIN_RESET);
  HAL_Delay(100U);
  HAL_GPIO_WritePin(Screen_RES_GPIO_Port, Screen_RES_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);

  for (index = 0U;
       index < (sizeof(s_ili9341_init_commands) / sizeof(s_ili9341_init_commands[0]));
       index++)
  {
    status = ILI9341_WriteCommand(s_ili9341_init_commands[index].command,
                                  s_ili9341_init_commands[index].data,
                                  s_ili9341_init_commands[index].length);
    if (status != HAL_OK)
    {
      return status;
    }

    if (s_ili9341_init_commands[index].delay_ms > 0U)
    {
      HAL_Delay(s_ili9341_init_commands[index].delay_ms);
    }
  }

  status = ILI9341_FillScreen(ILI9341_COLOR_BLACK);
  if (status == HAL_OK)
  {
    ILI9341_SetBacklight(1U);
  }

  return status;
}

/**
  * @brief 设置屏幕背光开关。
  * @param enable 为1时打开背光，为0时关闭背光。
  * @retval 无。
  */
void ILI9341_SetBacklight(uint8_t enable)
{
  HAL_GPIO_WritePin(Screen_BLK_GPIO_Port, Screen_BLK_Pin,
                    (enable != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
  * @brief 使用RGB565颜色填充整个屏幕。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ILI9341_FillScreen(uint16_t color)
{
  return ILI9341_FillRect(0U, 0U, ILI9341_WIDTH, ILI9341_HEIGHT, color);
}

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
                                  uint16_t color)
{
  HAL_StatusTypeDef status;
  uint16_t column;
  uint16_t row;

  if ((width == 0U) || (height == 0U))
  {
    return HAL_OK;
  }

  if ((x >= ILI9341_WIDTH) || (y >= ILI9341_HEIGHT))
  {
    return HAL_ERROR;
  }

  if (width > (ILI9341_WIDTH - x))
  {
    width = ILI9341_WIDTH - x;
  }
  if (height > (ILI9341_HEIGHT - y))
  {
    height = ILI9341_HEIGHT - y;
  }

  for (column = 0U; column < width; column++)
  {
    s_ili9341_line_buffer[column * 2U] = (uint8_t)(color >> 8U);
    s_ili9341_line_buffer[column * 2U + 1U] = (uint8_t)color;
  }

  status = ILI9341_SetAddressWindow(x, y,
                                    (uint16_t)(x + width - 1U),
                                    (uint16_t)(y + height - 1U));
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Screen_DC_GPIO_Port, Screen_DC_Pin, GPIO_PIN_SET);

  for (row = 0U; row < height; row++)
  {
    status = ILI9341_Transmit(s_ili9341_line_buffer, (uint16_t)(width * 2U));
    if (status != HAL_OK)
    {
      break;
    }
  }

  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_SET);
  return status;
}

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
                                     const uint8_t *pixel_data)
{
  HAL_StatusTypeDef status;
  uint32_t byte_count;
  uint32_t offset;
  uint16_t chunk_size;

  if ((pixel_data == NULL) || (width == 0U) || (height == 0U))
  {
    return HAL_ERROR;
  }

  if ((x >= ILI9341_WIDTH) || (y >= ILI9341_HEIGHT) ||
      (width > (ILI9341_WIDTH - x)) ||
      (height > (ILI9341_HEIGHT - y)))
  {
    return HAL_ERROR;
  }

  status = ILI9341_SetAddressWindow(x, y,
                                    (uint16_t)(x + width - 1U),
                                    (uint16_t)(y + height - 1U));
  if (status != HAL_OK)
  {
    return status;
  }

  byte_count = (uint32_t)width * height * 2U;
  offset = 0U;
  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Screen_DC_GPIO_Port, Screen_DC_Pin, GPIO_PIN_SET);

  while (offset < byte_count)
  {
    chunk_size = (uint16_t)(((byte_count - offset) > 65535U)
                                ? 65535U
                                : (byte_count - offset));
    status = ILI9341_Transmit(&pixel_data[offset], chunk_size);
    if (status != HAL_OK)
    {
      break;
    }
    offset += chunk_size;
  }

  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_SET);
  return status;
}

/**
  * @brief 在指定坐标绘制一个RGB565像素。
  * @param x 像素横坐标。
  * @param y 像素纵坐标。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  return ILI9341_FillRect(x, y, 1U, 1U, color);
}
