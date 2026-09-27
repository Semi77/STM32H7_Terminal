#include "screen_test.h"
#include "st7735s.h"

/**
  * @brief 依次显示基础颜色以检查屏幕通信和RGB565颜色顺序。
  * @param 无。
  * @retval HAL状态。
  */
HAL_StatusTypeDef Screen_Test_Run(void)
{
  static const uint16_t colors[] =
  {
    ST7735S_COLOR_RED,
    ST7735S_COLOR_GREEN,
    ST7735S_COLOR_BLUE,
    ST7735S_COLOR_WHITE,
    ST7735S_COLOR_BLACK
  };
  HAL_StatusTypeDef status;
  uint32_t index;

  for (index = 0U; index < (sizeof(colors) / sizeof(colors[0])); index++)
  {
    status = ST7735S_FillScreen(colors[index]);
    if (status != HAL_OK)
    {
      return status;
    }
    HAL_Delay(500U);
  }

  return ST7735S_FillScreen(ST7735S_COLOR_BLUE);
}
