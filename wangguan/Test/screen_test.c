#include "screen_test.h"
#include "ili9341.h"

/**
  * @brief 依次显示基础颜色以检查屏幕通信和RGB565颜色顺序。
  * @param 无。
  * @retval HAL状态。
  */
HAL_StatusTypeDef Screen_Test_Run(void)
{
  static const uint16_t colors[] =
  {
    ILI9341_COLOR_RED,
    ILI9341_COLOR_GREEN,
    ILI9341_COLOR_BLUE,
    ILI9341_COLOR_WHITE,
    ILI9341_COLOR_BLACK
  };
  HAL_StatusTypeDef status;
  uint32_t index;

  for (index = 0U; index < (sizeof(colors) / sizeof(colors[0])); index++)
  {
    status = ILI9341_FillScreen(colors[index]);
    if (status != HAL_OK)
    {
      return status;
    }
    HAL_Delay(500U);
  }

  return ILI9341_FillScreen(ILI9341_COLOR_BLUE);
}
