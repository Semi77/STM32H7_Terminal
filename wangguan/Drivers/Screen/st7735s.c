#include "st7735s.h"

#define ST7735S_SPI_TIMEOUT_MS  1000U
#define ST7735S_CMD_COLUMN_ADDR 0x2AU
#define ST7735S_CMD_PAGE_ADDR   0x2BU
#define ST7735S_CMD_MEMORY_WRITE 0x2CU
#define ST7735S_BACKLIGHT_FULL 1000U
#define ST7735S_BACKLIGHT_DIM 100U
#define ST7735S_BACKLIGHT_IDLE_MS 5000U

typedef struct
{
  uint8_t command;
  uint8_t length;
  uint8_t data[16];
  uint16_t delay_ms;
} ST7735S_InitCommandTypeDef;

static SPI_HandleTypeDef *s_st7735s_spi;
static uint8_t s_st7735s_line_buffer[ST7735S_WIDTH * 2U];
static TIM_HandleTypeDef s_backlight_timer;
static uint32_t s_backlight_activity_tick;
static uint8_t s_backlight_ready;
static uint8_t s_backlight_dimmed;

/**
  * @brief 将PB1配置为TIM3通道4的1kHz PWM背光输出。
  * @retval HAL_OK表示PWM已启动，其他状态表示定时器初始化失败。
  */
static HAL_StatusTypeDef ST7735S_InitBacklight(void)
{
  GPIO_InitTypeDef gpio = {0};
  TIM_OC_InitTypeDef output = {0};

  HAL_GPIO_WritePin(Screen_BLK_GPIO_Port, Screen_BLK_Pin, GPIO_PIN_RESET);
  __HAL_RCC_TIM3_CLK_ENABLE();
  s_backlight_timer.Instance = TIM3;
  s_backlight_timer.Init.Prescaler = 119U;
  s_backlight_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
  s_backlight_timer.Init.Period = 999U;
  s_backlight_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  s_backlight_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&s_backlight_timer) != HAL_OK)
  {
    return HAL_ERROR;
  }

  output.OCMode = TIM_OCMODE_PWM1;
  output.Pulse = 0U;
  output.OCPolarity = TIM_OCPOLARITY_HIGH;
  output.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&s_backlight_timer, &output, TIM_CHANNEL_4) != HAL_OK)
  {
    return HAL_ERROR;
  }

  gpio.Pin = Screen_BLK_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(Screen_BLK_GPIO_Port, &gpio);
  if (HAL_TIM_PWM_Start(&s_backlight_timer, TIM_CHANNEL_4) != HAL_OK)
  {
    return HAL_ERROR;
  }

  s_backlight_ready = 1U;
  return HAL_OK;
}

/* 按ST7735S的RGB565竖屏时序初始化显示控制器。 */
static const ST7735S_InitCommandTypeDef s_st7735s_init_commands[] =
{
  {0x01U, 0U, {0U}, 150U},
  {0x11U, 0U, {0U}, 120U},
  {0xB1U, 3U, {0x05U, 0x3CU, 0x3CU}, 0U},
  {0xB2U, 3U, {0x05U, 0x3CU, 0x3CU}, 0U},
  {0xB3U, 6U, {0x05U, 0x3CU, 0x3CU, 0x05U, 0x3CU, 0x3CU}, 0U},
  {0xB4U, 1U, {0x03U}, 0U},
  {0xC0U, 3U, {0x28U, 0x08U, 0x04U}, 0U},
  {0xC1U, 1U, {0xC0U}, 0U},
  {0xC2U, 2U, {0x0DU, 0x00U}, 0U},
  {0xC3U, 2U, {0x8DU, 0x2AU}, 0U},
  {0xC4U, 2U, {0x8DU, 0xEEU}, 0U},
  {0xC5U, 1U, {0x10U}, 0U},
  {0x3AU, 1U, {0x55U}, 0U},
  {0x36U, 1U, {0xC8U}, 0U},
  {0xE0U, 16U, {0x04U, 0x22U, 0x07U, 0x0AU, 0x2EU, 0x30U, 0x25U, 0x2AU,
                 0x28U, 0x26U, 0x2EU, 0x3AU, 0x00U, 0x01U, 0x03U, 0x13U}, 0U},
  {0xE1U, 16U, {0x04U, 0x16U, 0x06U, 0x0DU, 0x2DU, 0x26U, 0x23U, 0x27U,
                 0x27U, 0x25U, 0x2DU, 0x3BU, 0x00U, 0x01U, 0x04U, 0x13U}, 0U},
  {0x13U, 0U, {0U}, 10U},
  {0x29U, 0U, {0U}, 20U}
};

/**
  * @brief 通过SPI发送一段字节数据。
  * @param data 待发送数据地址。
  * @param length 待发送字节数。
  * @retval HAL状态。
  */
static HAL_StatusTypeDef ST7735S_Transmit(const uint8_t *data, uint16_t length)
{
  if ((s_st7735s_spi == NULL) || (data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  return HAL_SPI_Transmit(s_st7735s_spi, (uint8_t *)data,
                          length, ST7735S_SPI_TIMEOUT_MS);
}

/**
  * @brief 向ST7735S发送一个命令及其参数。
  * @param command 命令字节。
  * @param data 命令参数地址，无参数时传入NULL。
  * @param length 命令参数字节数。
  * @retval HAL状态。
  */
static HAL_StatusTypeDef ST7735S_WriteCommand(uint8_t command,
                                              const uint8_t *data,
                                              uint16_t length)
{
  HAL_StatusTypeDef status;

  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Screen_DC_GPIO_Port, Screen_DC_Pin, GPIO_PIN_RESET);

  status = ST7735S_Transmit(&command, 1U);
  if ((status == HAL_OK) && (length > 0U))
  {
    HAL_GPIO_WritePin(Screen_DC_GPIO_Port, Screen_DC_Pin, GPIO_PIN_SET);
    status = ST7735S_Transmit(data, length);
  }

  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_SET);
  return status;
}

/**
  * @brief 设置ST7735S后续显存写入的坐标窗口。
  * @param x_start 起始横坐标。
  * @param y_start 起始纵坐标。
  * @param x_end 结束横坐标且包含该坐标。
  * @param y_end 结束纵坐标且包含该坐标。
  * @retval HAL状态。
  */
static HAL_StatusTypeDef ST7735S_SetAddressWindow(uint16_t x_start,
                                                  uint16_t y_start,
                                                  uint16_t x_end,
                                                  uint16_t y_end)
{
  uint8_t address[4];
  HAL_StatusTypeDef status;

  /* 将逻辑坐标映射到模组实际使用的显存区域。 */
  x_start += ST7735S_X_OFFSET;
  x_end += ST7735S_X_OFFSET;
  y_start += ST7735S_Y_OFFSET;
  y_end += ST7735S_Y_OFFSET;

  address[0] = (uint8_t)(x_start >> 8U);
  address[1] = (uint8_t)x_start;
  address[2] = (uint8_t)(x_end >> 8U);
  address[3] = (uint8_t)x_end;
  status = ST7735S_WriteCommand(ST7735S_CMD_COLUMN_ADDR, address, sizeof(address));
  if (status != HAL_OK)
  {
    return status;
  }

  address[0] = (uint8_t)(y_start >> 8U);
  address[1] = (uint8_t)y_start;
  address[2] = (uint8_t)(y_end >> 8U);
  address[3] = (uint8_t)y_end;
  status = ST7735S_WriteCommand(ST7735S_CMD_PAGE_ADDR, address, sizeof(address));
  if (status != HAL_OK)
  {
    return status;
  }

  return ST7735S_WriteCommand(ST7735S_CMD_MEMORY_WRITE, NULL, 0U);
}

/**
  * @brief 初始化ST7735S屏幕并清为黑色。
  * @param hspi 屏幕连接的SPI句柄。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ST7735S_Init(SPI_HandleTypeDef *hspi)
{
  HAL_StatusTypeDef status;
  uint32_t index;

  if (hspi == NULL)
  {
    return HAL_ERROR;
  }

  s_st7735s_spi = hspi;
  if (ST7735S_InitBacklight() != HAL_OK)
  {
    return HAL_ERROR;
  }
  ST7735S_SetBacklight(0U);
  HAL_GPIO_WritePin(Screen_CS_GPIO_Port, Screen_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(Screen_DC_GPIO_Port, Screen_DC_Pin, GPIO_PIN_RESET);

  HAL_GPIO_WritePin(Screen_RES_GPIO_Port, Screen_RES_Pin, GPIO_PIN_RESET);
  HAL_Delay(100U);
  HAL_GPIO_WritePin(Screen_RES_GPIO_Port, Screen_RES_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);

  for (index = 0U;
       index < (sizeof(s_st7735s_init_commands) / sizeof(s_st7735s_init_commands[0]));
       index++)
  {
    status = ST7735S_WriteCommand(s_st7735s_init_commands[index].command,
                                  s_st7735s_init_commands[index].data,
                                  s_st7735s_init_commands[index].length);
    if (status != HAL_OK)
    {
      return status;
    }

    if (s_st7735s_init_commands[index].delay_ms > 0U)
    {
      HAL_Delay(s_st7735s_init_commands[index].delay_ms);
    }
  }

  status = ST7735S_FillScreen(ST7735S_COLOR_BLACK);
  if (status == HAL_OK)
  {
    ST7735S_SetBacklight(1U);
  }

  return status;
}

/**
  * @brief 通过PWM全亮或关闭屏幕背光。
  * @param enable 非零时全亮，为零时关闭背光。
  * @retval 无。
  */
void ST7735S_SetBacklight(uint8_t enable)
{
  if (s_backlight_ready == 0U)
  {
    return;
  }
  __HAL_TIM_SET_COMPARE(&s_backlight_timer, TIM_CHANNEL_4,
                        enable != 0U ? ST7735S_BACKLIGHT_FULL : 0U);
  s_backlight_activity_tick = HAL_GetTick();
  s_backlight_dimmed = 0U;
}

/**
  * @brief 上位机有效指令到达后将背光恢复全亮并重新开始五秒计时。
  * @retval 无，必须从图形任务调用。
  */
void ST7735S_BacklightActivity(void)
{
  ST7735S_SetBacklight(1U);
}

/**
  * @brief 五秒没有新的有效指令时将背光降到百分之十亮度。
  * @retval 无，必须从图形任务周期调用。
  */
void ST7735S_BacklightPoll(void)
{
  if (s_backlight_ready != 0U && s_backlight_dimmed == 0U &&
      (uint32_t)(HAL_GetTick() - s_backlight_activity_tick) >= ST7735S_BACKLIGHT_IDLE_MS)
  {
    __HAL_TIM_SET_COMPARE(&s_backlight_timer, TIM_CHANNEL_4, ST7735S_BACKLIGHT_DIM);
    s_backlight_dimmed = 1U;
  }
}

/**
  * @brief 使用RGB565颜色填充整个屏幕。
  * @param color RGB565颜色值。
  * @retval HAL状态。
  */
HAL_StatusTypeDef ST7735S_FillScreen(uint16_t color)
{
  return ST7735S_FillRect(0U, 0U, ST7735S_WIDTH, ST7735S_HEIGHT, color);
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
HAL_StatusTypeDef ST7735S_FillRect(uint16_t x, uint16_t y,
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

  if ((x >= ST7735S_WIDTH) || (y >= ST7735S_HEIGHT))
  {
    return HAL_ERROR;
  }

  if (width > (ST7735S_WIDTH - x))
  {
    width = ST7735S_WIDTH - x;
  }
  if (height > (ST7735S_HEIGHT - y))
  {
    height = ST7735S_HEIGHT - y;
  }

  for (column = 0U; column < width; column++)
  {
    s_st7735s_line_buffer[column * 2U] = (uint8_t)(color >> 8U);
    s_st7735s_line_buffer[column * 2U + 1U] = (uint8_t)color;
  }

  status = ST7735S_SetAddressWindow(x, y,
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
    status = ST7735S_Transmit(s_st7735s_line_buffer, (uint16_t)(width * 2U));
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
HAL_StatusTypeDef ST7735S_WriteRGB565(uint16_t x, uint16_t y,
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

  if ((x >= ST7735S_WIDTH) || (y >= ST7735S_HEIGHT) ||
      (width > (ST7735S_WIDTH - x)) ||
      (height > (ST7735S_HEIGHT - y)))
  {
    return HAL_ERROR;
  }

  status = ST7735S_SetAddressWindow(x, y,
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
    status = ST7735S_Transmit(&pixel_data[offset], chunk_size);
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
HAL_StatusTypeDef ST7735S_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  return ST7735S_FillRect(x, y, 1U, 1U, color);
}
