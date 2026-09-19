#ifndef DHT11_H
#define DHT11_H

#include "main.h"

typedef struct
{
  int16_t temperature_tenths_c;
  uint16_t humidity_tenths_percent;
} DHT11_DataTypeDef;

/**
  * @brief 初始化DWT计时并释放CubeMX配置的PE5开漏数据线。
  * @retval HAL_OK表示计时正常，HAL_ERROR表示DWT不可用。
  */
HAL_StatusTypeDef DHT11_Init(void);

/**
  * @brief 在FreeRTOS任务中读取DHT11，成功才写入数据，失败保留调用者原值。
  * @param data 接收温湿度的结构体，单位分别为0.1摄氏度和0.1百分比。
  * @retval HAL_OK成功，HAL_BUSY未到读取时间，HAL_TIMEOUT时序超时，HAL_ERROR校验或参数错误。
  */
HAL_StatusTypeDef DHT11_Read(DHT11_DataTypeDef *data);

#endif
