#ifndef BH1750_H
#define BH1750_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t lux;
  uint32_t sequence;
  bool has_data;
  bool read_ok;
} BH1750_Sample;

/**
  * @brief 启动BH1750连续高精度采集任务。
  * @param hi2c 已初始化的I2C1句柄，ADDR引脚需接地。
  * @retval HAL_OK表示任务创建成功，HAL_ERROR表示参数错误或任务创建失败。
  */
HAL_StatusTypeDef BH1750_Start(I2C_HandleTypeDef *hi2c);

/**
  * @brief 读取最近一次照度和通信状态快照。
  * @param sample 接收快照的非空指针。
  * @retval true表示参数有效，false表示参数为空。
  */
bool BH1750_GetSample(BH1750_Sample *sample);

#endif
