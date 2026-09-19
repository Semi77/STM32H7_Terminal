#include "dht11.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

CoreDebug_Type mock_core_debug;
uint32_t SystemCoreClock = 1000000U;
static DWT_Type mock_dwt;
static uint64_t now_us;
static uint64_t released_us;
static uint64_t masked_since;
static uint64_t max_masked_us;
static uint32_t primask;
static int scheduler_depth;
static bool clock_stopped;
static bool output_low;
static bool transmitting;
static int line_mode;
static uint8_t tx_frame[5];
static unsigned int tx_bits = 40U;
static uint32_t zero_high_us = 27U;
static uint32_t one_high_us = 70U;

/**
  * @brief 模拟自增DWT计数，允许测试32位计数回绕及计时器停止。
  * @retval 模拟的DWT寄存器地址。
  */
DWT_Type *Mock_DWT(void)
{
  now_us++;
  if (!clock_stopped) mock_dwt.CYCCNT = (uint32_t)now_us;
  return &mock_dwt;
}

/** @brief 返回模拟的中断屏蔽状态。 @retval PRIMASK值。 */
uint32_t Mock_GetPrimask(void) { return primask; }

/** @brief 更新中断屏蔽状态并统计最长连续屏蔽时间。 @param value 新PRIMASK值。 */
void Mock_SetPrimask(uint32_t value)
{
  if (!primask && value) masked_since = now_us;
  if (primask && !value && now_us - masked_since > max_masked_us)
    max_masked_us = now_us - masked_since;
  primask = value;
}

/** @brief 返回模拟毫秒时钟并保留32位回绕。 @retval 毫秒计数。 */
uint32_t HAL_GetTick(void) { return (uint32_t)(now_us / 1000U); }

/** @brief 模拟起始低电平延时并检查此时没有关中断或暂停调度。 @param ms 延时毫秒数。 */
void HAL_Delay(uint32_t ms)
{
  assert(primask == 0U && scheduler_depth == 0);
  now_us += (uint64_t)ms * 1000U;
}

/** @brief 模拟开漏GPIO的拉低和释放。 @param port 未使用。 @param pin 引脚掩码。 @param state 输出状态。 */
void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState state)
{
  (void)port;
  assert(pin == DHT_Pin);
  if (state == GPIO_PIN_RESET) output_low = true;
  else if (output_low)
  {
    output_low = false;
    transmitting = true;
    released_us = now_us;
  }
}

/**
  * @brief 根据传感器应答和40位波形生成DATA电平，支持断线及截断帧。
  * @param port 未使用。
  * @param pin 引脚掩码。
  * @retval 当前模拟电平。
  */
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{
  uint64_t elapsed;
  unsigned int index;
  (void)port;
  assert(pin == DHT_Pin);
  now_us++;
  if (output_low || line_mode == 2) return GPIO_PIN_RESET;
  if (!transmitting || line_mode == 1) return GPIO_PIN_SET;
  elapsed = now_us - released_us;
  if (elapsed < 30U) return GPIO_PIN_SET;
  elapsed -= 30U;
  if (elapsed < 80U) return GPIO_PIN_RESET;
  elapsed -= 80U;
  if (elapsed < 80U) return GPIO_PIN_SET;
  elapsed -= 80U;
  for (index = 0U; index < tx_bits; index++)
  {
    uint32_t high_us;
    if (elapsed < 50U) return GPIO_PIN_RESET;
    elapsed -= 50U;
    high_us = (tx_frame[index / 8U] & (0x80U >> (index % 8U))) ? one_high_us : zero_high_us;
    if (elapsed < high_us) return GPIO_PIN_SET;
    elapsed -= high_us;
  }
  if (tx_bits < 40U) return GPIO_PIN_SET;
  return elapsed < 50U ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

/** @brief 记录模拟的调度暂停深度。 */
void vTaskSuspendAll(void) { scheduler_depth++; }

/** @brief 验证接收结束后恢复调度。 @retval 零。 */
int xTaskResumeAll(void) { assert(scheduler_depth == 1); scheduler_depth--; return 0; }

/** @brief 为一个独立用例设置模拟时钟和总线。 @param start_us 初始微秒时间。 */
static void reset_case(uint64_t start_us)
{
  now_us = start_us;
  primask = 0U;
  scheduler_depth = 0;
  output_low = transmitting = clock_stopped = false;
  line_mode = 0;
  tx_bits = 40U;
  max_masked_us = 0U;
  memset(&mock_dwt, 0, sizeof(mock_dwt));
  assert(DHT11_Init() == HAL_OK);
  now_us += 1000000U;
}

/** @brief 装载五字节测试帧。 @param frame 从传感器发送的字节数组。 */
static void load_frame(const uint8_t frame[5]) { memcpy(tx_frame, frame, 5U); }

/** @brief 验证错误没有覆盖旧值、没有锁住调度或数据线。 @param expected 预期返回值。 */
static void expect_failure(HAL_StatusTypeDef expected)
{
  DHT11_DataTypeDef data = {251, 612};
  assert(DHT11_Read(&data) == expected);
  assert(data.temperature_tenths_c == 251 && data.humidity_tenths_percent == 612);
  assert(primask == 0U && scheduler_depth == 0 && !output_low);
  assert(max_masked_us < 350U);
}

/** @brief 执行真实驱动的协议波形、校验、异常恢复和时钟回绕测试。 @retval 零表示通过。 */
int main(void)
{
  const uint8_t normal[5] = {55U, 0U, 24U, 0U, 79U};
  const uint8_t corrupt[5] = {55U, 0U, 24U, 0U, 78U};
  const uint8_t zero[5] = {0U, 0U, 0U, 0U, 0U};
  /* 人工边界帧只用于验证位顺序和校验和取低八位，不代表真实测量范围。 */
  const uint8_t carry[5] = {255U, 255U, 255U, 255U, 252U};
  DHT11_DataTypeDef data = {0};
  unsigned int pulse;

  reset_case(0U);
  load_frame(normal);
  assert(DHT11_Read(&data) == HAL_OK);
  assert(data.temperature_tenths_c == 240 && data.humidity_tenths_percent == 550);
  assert(max_masked_us < 350U);
  expect_failure(HAL_BUSY);
  now_us += 2000000U;
  load_frame(corrupt);
  expect_failure(HAL_ERROR);
  now_us += 2000000U;
  load_frame(normal);
  assert(DHT11_Read(&data) == HAL_OK);

  reset_case(0U);
  load_frame(zero);
  assert(DHT11_Read(&data) == HAL_OK);
  assert(data.temperature_tenths_c == 0 && data.humidity_tenths_percent == 0);
  reset_case(0U);
  load_frame(carry);
  assert(DHT11_Read(&data) == HAL_OK);
  assert(data.temperature_tenths_c == 2805 && data.humidity_tenths_percent == 2805);

  reset_case(0U); line_mode = 1; expect_failure(HAL_TIMEOUT);
  reset_case(0U); line_mode = 2; expect_failure(HAL_TIMEOUT);
  reset_case(0U); load_frame(normal); tx_bits = 17U; expect_failure(HAL_TIMEOUT);

  for (pulse = 26U; pulse <= 28U; pulse++)
  {
    reset_case(0U); zero_high_us = pulse; load_frame(normal);
    assert(DHT11_Read(&data) == HAL_OK);
    assert(data.temperature_tenths_c == 240 && data.humidity_tenths_percent == 550);
  }

  reset_case((uint64_t)UINT32_MAX - 1022000U);
  load_frame(normal);
  assert(DHT11_Read(&data) == HAL_OK);
  reset_case(((uint64_t)UINT32_MAX - 1500U) * 1000U);
  load_frame(normal);
  assert(DHT11_Read(&data) == HAL_OK);
  now_us += 2000000U;
  assert(DHT11_Read(&data) == HAL_OK);

  reset_case(0U);
  assert(DHT11_Init() == HAL_OK);
  expect_failure(HAL_BUSY);
  assert(DHT11_Read(NULL) == HAL_ERROR);
  clock_stopped = true;
  assert(DHT11_Init() == HAL_ERROR);
  expect_failure(HAL_ERROR);
  puts("PASS: valid/zero/carry frames, checksum discard/recovery, rate limit, no response, stuck low, truncated frame, pulse widths, DWT/tick rollover, init failure; IRQ/scheduler cleanup verified.");
  return 0;
}
