#include "cpu_stats.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx_hal.h"
#include <string.h>

/* TIM2独占用于统计，32位硬件计数器不产生周期中断。 */
static TIM_HandleTypeDef timer;
static bool started;
static uint32_t previous;
static uint64_t accumulated;

/** @brief 配置TIM2为1MHz自由运行计数器，重复调用不清零统计。 @retval 无。 */
void CpuStats_Init(void)
{
    if (started) return;
    RCC_ClkInitTypeDef clocks;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clocks, &latency);
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    if (clocks.APB1CLKDivider != RCC_HCLK_DIV1) clock *= 2U;
    __HAL_RCC_TIM2_CLK_ENABLE();
    timer.Instance = TIM2;
    timer.Init.Prescaler = clock / 1000000U - 1U;
    timer.Init.Period = UINT32_MAX;
    timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_StatusTypeDef status = HAL_TIM_Base_Init(&timer);
    configASSERT(status == HAL_OK);
    status = HAL_TIM_Base_Start(&timer);
    configASSERT(status == HAL_OK);
    started = true;
}

/**
  * @brief 将TIM2计数扩展为64位微秒时间，任务切换持续采样以覆盖32位回绕。
  * @retval 累计微秒数；相邻调用间隔须小于TIM2的约71分钟回绕周期。
  */
uint64_t CpuStats_GetCounter(void)
{
    if (!started) return 0;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t now = __HAL_TIM_GET_COUNTER(&timer);
    accumulated += (uint32_t)(now - previous);
    previous = now;
    uint64_t result = accumulated;
    __set_PRIMASK(mask);
    return result;
}

/**
  * @brief 用相邻快照的总时间和IDLE时间差计算最近采样窗口的CPU占用率。
  * @param sample 零初始化的上次快照；tasks/count为本次任务快照及数量。
  * @param elapsed 本次累计微秒数；percent仅在有效时写入0至100的百分比。
  * @retval 首次采样、无IDLE或时间异常返回false，保留上次显示值。
  */
bool CpuStats_Update(CpuStatsSample *sample, const CpuStatsTask *tasks,
                     uint32_t count, uint64_t elapsed, uint32_t *percent)
{
    if (!sample || !tasks || !count || !percent) return false;
    uint64_t idle = 0;
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(tasks[i].name, "IDLE") == 0) {
            idle = tasks[i].run_time;
            found = true;
            break;
        }
    }
    if (!found || idle > elapsed) { sample->valid = false; return false; }
    bool valid = sample->valid && elapsed > sample->elapsed && idle >= sample->idle;
    uint64_t total_delta = elapsed - sample->elapsed;
    uint64_t idle_delta = idle - sample->idle;
    sample->elapsed = elapsed;
    sample->idle = idle;
    sample->valid = true;
    if (!valid || idle_delta > total_delta) return false;
    /* 不依赖任务时间总和，已退出的自检任务不会破坏统计。 */
    *percent = (uint32_t)((total_delta - idle_delta) * 100U / total_delta);
    return true;
}
