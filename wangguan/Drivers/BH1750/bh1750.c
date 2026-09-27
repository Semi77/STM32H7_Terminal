#include "bh1750.h"

#include "FreeRTOS.h"
#include "task.h"

#define BH1750_ADDRESS        (0x23U << 1)
#define BH1750_CONTINUOUS_HR  0x10U
#define BH1750_I2C_TIMEOUT_MS 20U
#define BH1750_FIRST_WAIT_MS  200U
#define BH1750_SAMPLE_MS      250U

static I2C_HandleTypeDef *s_hi2c;
static BH1750_Sample s_sample;

/**
  * @brief 记录一次测量结果或通信失败，失败时保留上次有效照度。
  * @param read_ok 本次I2C读取是否成功。
  * @param lux 本次测得的照度，单位勒克斯，失败时忽略。
  * @retval 无。
  */
static void BH1750_StoreSample(bool read_ok, uint32_t lux)
{
  taskENTER_CRITICAL();
  if (read_ok)
  {
    s_sample.lux = lux;
    s_sample.has_data = true;
  }
  s_sample.read_ok = read_ok;
  ++s_sample.sequence;
  taskEXIT_CRITICAL();
}

/**
  * @brief 启动连续测量并定期读取高低两个字节，通信失败后重新初始化传感器。
  * @param argument 未使用的任务参数。
  * @retval 无。
  */
static void BH1750_Task(void *argument)
{
  uint8_t command = BH1750_CONTINUOUS_HR;
  uint8_t data[2];
  (void)argument;

  for (;;)
  {
    if (HAL_I2C_IsDeviceReady(s_hi2c, BH1750_ADDRESS, 1U,
                              BH1750_I2C_TIMEOUT_MS) != HAL_OK ||
        HAL_I2C_Master_Transmit(s_hi2c, BH1750_ADDRESS, &command, 1U,
                                BH1750_I2C_TIMEOUT_MS) != HAL_OK)
    {
      BH1750_StoreSample(false, 0U);
      vTaskDelay(pdMS_TO_TICKS(BH1750_SAMPLE_MS));
      continue;
    }

    /* 首次高精度转换最长180毫秒，额外留出任务节拍余量。 */
    vTaskDelay(pdMS_TO_TICKS(BH1750_FIRST_WAIT_MS));
    for (;;)
    {
      if (HAL_I2C_Master_Receive(s_hi2c, BH1750_ADDRESS, data, 2U,
                                 BH1750_I2C_TIMEOUT_MS) != HAL_OK)
      {
        BH1750_StoreSample(false, 0U);
        break;
      }

      /* 默认测量时间下按数据手册除以1.2，整数运算保留四舍五入后的lux。 */
      uint32_t raw = ((uint32_t)data[0] << 8) | data[1];
      BH1750_StoreSample(true, (raw * 5U + 3U) / 6U);
      vTaskDelay(pdMS_TO_TICKS(BH1750_SAMPLE_MS));
    }
  }
}

/**
  * @brief 启动BH1750连续高精度采集任务。
  * @param hi2c 已初始化的I2C1句柄，ADDR引脚需接地。
  * @retval HAL_OK表示任务创建成功，HAL_ERROR表示参数错误或任务创建失败。
  */
HAL_StatusTypeDef BH1750_Start(I2C_HandleTypeDef *hi2c)
{
  if (hi2c == NULL || hi2c->Instance != I2C1 || s_hi2c != NULL)
  {
    return HAL_ERROR;
  }

  s_hi2c = hi2c;
  if (xTaskCreate(BH1750_Task, "bh1750Task", 1024U / sizeof(StackType_t),
                  NULL, 6U, NULL) != pdPASS)
  {
    s_hi2c = NULL;
    return HAL_ERROR;
  }
  return HAL_OK;
}

/**
  * @brief 读取最近一次照度和通信状态快照。
  * @param sample 接收快照的非空指针。
  * @retval true表示参数有效，false表示参数为空。
  */
bool BH1750_GetSample(BH1750_Sample *sample)
{
  if (sample == NULL)
  {
    return false;
  }

  taskENTER_CRITICAL();
  *sample = s_sample;
  taskEXIT_CRITICAL();
  return true;
}
