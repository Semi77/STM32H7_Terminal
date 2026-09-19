#include "dht11.h"

#include "FreeRTOS.h"
#include "task.h"

#define DHT11_POWER_ON_DELAY_MS 1000U
#define DHT11_SAMPLE_INTERVAL_MS 2000U
#define DHT11_START_LOW_MS 20U
#define DHT11_EDGE_TIMEOUT_US 100U
#define DHT11_ONE_THRESHOLD_US 50U
#define DHT11_DWT_UNLOCK_KEY 0xC5ACCE55UL

static uint32_t s_cycles_per_us;
static uint32_t s_last_read_tick;
static uint32_t s_read_interval_ms;

/**
  * @brief 使用DWT限制等待时间，等待DATA到达目标电平。
  * @param level 需要等待的GPIO电平。
  * @retval HAL_OK表示已到达目标电平，HAL_TIMEOUT表示等待超时。
  */
static HAL_StatusTypeDef DHT11_WaitLevel(GPIO_PinState level)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t timeout = s_cycles_per_us * DHT11_EDGE_TIMEOUT_US;

  while (HAL_GPIO_ReadPin(DHT_GPIO_Port, DHT_Pin) != level)
  {
    if ((uint32_t)(DWT->CYCCNT - start) >= timeout)
    {
      return HAL_TIMEOUT;
    }
  }
  return HAL_OK;
}

/**
  * @brief 释放总线并等待DHT11完成低电平及高电平应答。
  * @retval HAL_OK表示应答完成，HAL_TIMEOUT表示传感器未应答或应答未结束。
  */
static HAL_StatusTypeDef DHT11_ReadResponse(void)
{
  HAL_StatusTypeDef status;
  uint32_t primask = __get_PRIMASK();

  /* 仅在应答阶段关闭中断，各等待均有100微秒超时。 */
  __disable_irq();
  HAL_GPIO_WritePin(DHT_GPIO_Port, DHT_Pin, GPIO_PIN_SET);
  status = DHT11_WaitLevel(GPIO_PIN_RESET);
  if (status == HAL_OK)
  {
    status = DHT11_WaitLevel(GPIO_PIN_SET);
  }
  if (status == HAL_OK)
  {
    status = DHT11_WaitLevel(GPIO_PIN_RESET);
  }
  __set_PRIMASK(primask);
  return status;
}

/**
  * @brief 测量一位数据的高电平脉宽，短脉冲为0，长脉冲为1。
  * @param bit 接收本次解析出的二进制位，仅成功时写入。
  * @retval HAL_OK表示接收成功，HAL_TIMEOUT表示电平等待超时。
  */
static HAL_StatusTypeDef DHT11_ReadBit(uint8_t *bit)
{
  HAL_StatusTypeDef status;
  uint32_t start;
  uint32_t high_cycles = 0U;
  uint32_t primask = __get_PRIMASK();

  /* 每位结束即恢复中断，让系统时基在相邻位的低电平期间得到服务。 */
  __disable_irq();
  status = DHT11_WaitLevel(GPIO_PIN_SET);
  if (status == HAL_OK)
  {
    start = DWT->CYCCNT;
    status = DHT11_WaitLevel(GPIO_PIN_RESET);
    high_cycles = (uint32_t)(DWT->CYCCNT - start);
  }
  __set_PRIMASK(primask);

  if (status == HAL_OK)
  {
    *bit = (high_cycles >= s_cycles_per_us * DHT11_ONE_THRESHOLD_US) ? 1U : 0U;
  }
  return status;
}

/**
  * @brief 初始化DWT计时并释放PE5，预留至少一秒的传感器上电稳定时间。
  * @retval HAL_OK表示计时正常，HAL_ERROR表示DWT不可用。
  */
HAL_StatusTypeDef DHT11_Init(void)
{
  uint32_t start;

  s_cycles_per_us = 0U;
  HAL_GPIO_WritePin(DHT_GPIO_Port, DHT_Pin, GPIO_PIN_SET);
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = DHT11_DWT_UNLOCK_KEY;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  start = DWT->CYCCNT;
  __DSB();
  __ISB();
  __NOP();
  __NOP();
  if ((DWT->CYCCNT == start) || (SystemCoreClock < 1000000U))
  {
    return HAL_ERROR;
  }

  s_cycles_per_us = SystemCoreClock / 1000000U;
  s_last_read_tick = HAL_GetTick();
  s_read_interval_ms = DHT11_POWER_ON_DELAY_MS;
  return HAL_OK;
}

/**
  * @brief 接收高位在前的五字节数据，校验通过后才发布温湿度。
  * @param data 接收温湿度的结构体，失败时内容保持不变；仅允许单个任务调用。
  * @retval HAL_OK成功，HAL_BUSY读取过早，HAL_TIMEOUT时序超时，HAL_ERROR校验或参数错误。
  */
HAL_StatusTypeDef DHT11_Read(DHT11_DataTypeDef *data)
{
  uint8_t frame[5] = {0U};
  uint8_t bit;
  uint32_t index;
  HAL_StatusTypeDef status;

  if ((data == NULL) || (s_cycles_per_us == 0U))
  {
    return HAL_ERROR;
  }
  if ((uint32_t)(HAL_GetTick() - s_last_read_tick) < s_read_interval_ms)
  {
    return HAL_BUSY;
  }

  s_last_read_tick = HAL_GetTick();
  s_read_interval_ms = DHT11_SAMPLE_INTERVAL_MS;
  HAL_GPIO_WritePin(DHT_GPIO_Port, DHT_Pin, GPIO_PIN_RESET);
  /* 起始低电平期间保持中断和调度开启，不占用微秒临界区。 */
  HAL_Delay(DHT11_START_LOW_MS);

  /* 接收期间暂停任务切换，但在位间开放中断；ISR必须短于约50微秒低电平窗口。 */
  vTaskSuspendAll();
  status = DHT11_ReadResponse();
  for (index = 0U; (index < 40U) && (status == HAL_OK); index++)
  {
    status = DHT11_ReadBit(&bit);
    if (status == HAL_OK)
    {
      frame[index / 8U] = (uint8_t)((frame[index / 8U] << 1U) | bit);
    }
  }
  HAL_GPIO_WritePin(DHT_GPIO_Port, DHT_Pin, GPIO_PIN_SET);
  (void)xTaskResumeAll();

  if (status != HAL_OK)
  {
    return status;
  }
  if ((uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]) != frame[4])
  {
    return HAL_ERROR;
  }

  /* 按DHT11整数和小数字节解析，本资料所述型号的小数字节为零。 */
  data->humidity_tenths_percent = (uint16_t)(frame[0] * 10U + frame[1]);
  data->temperature_tenths_c = (int16_t)(frame[2] * 10U + frame[3]);
  return HAL_OK;
}
