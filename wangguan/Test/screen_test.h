#ifndef SCREEN_TEST_H
#define SCREEN_TEST_H

#include "main.h"

/**
  * @brief 依次显示基础颜色以检查屏幕通信和RGB565颜色顺序。
  * @param 无。
  * @retval HAL状态。
  */
HAL_StatusTypeDef Screen_Test_Run(void);

#endif
