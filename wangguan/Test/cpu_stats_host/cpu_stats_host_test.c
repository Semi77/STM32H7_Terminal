#include "cpu_stats.h"
#include "stm32h7xx_hal.h"
#include <assert.h>
#include <stdio.h>
uint32_t mock_counter, mock_mask;
static unsigned init_calls;
/** @brief 模拟当前APB1分频，config/latency为时钟输出。 @retval 无。 */
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *config,uint32_t *latency) { config->APB1CLKDivider=1; *latency=0; }
/** @brief 返回测试使用的APB1频率。 @retval 60MHz。 */
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 60000000; }
/** @brief 检查硬件计数范围和分频，handle为被测配置。 @retval HAL_OK。 */
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *handle) {
    assert(handle->Instance==TIM2 && handle->Init.Period==UINT32_MAX && handle->Init.Prescaler==119);
    ++init_calls; return HAL_OK;
}
/** @brief 模拟启动，handle为计时器。 @retval HAL_OK。 */
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *handle) { (void)handle; return HAL_OK; }
/** @brief 验证硬件回绕、64位任务时间、窗口负载、已删任务和非法快照。 @retval 0表示通过。 */
int main(void) {
    assert(CpuStats_GetCounter()==0);
    CpuStats_Init(); CpuStats_Init(); assert(init_calls==1);
    mock_counter=UINT32_MAX-5; assert(CpuStats_GetCounter()==UINT32_MAX-5ULL);
    mock_counter=20; assert(CpuStats_GetCounter()==(1ULL<<32)+20);
    mock_mask=1; mock_counter=21; (void)CpuStats_GetCounter(); assert(mock_mask==1);
    CpuStatsSample sample={0}; CpuStatsTask tasks[2]={{"IDLE",0},{"GUI",0}}; uint32_t percent=73;
    assert(!CpuStats_Update(&sample,tasks,2,0,&percent) && percent==73);
    tasks[0].run_time=800; assert(CpuStats_Update(&sample,tasks,2,1000,&percent) && percent==20);
    tasks[0].run_time=900; assert(CpuStats_Update(&sample,tasks,1,2000,&percent) && percent==90);
    tasks[0].run_time=1900; assert(CpuStats_Update(&sample,tasks,1,3000,&percent) && percent==0);
    assert(CpuStats_Update(&sample,tasks,1,4000,&percent) && percent==100);
    percent=17; tasks[0].run_time=1000;
    assert(!CpuStats_Update(&sample,tasks,1,50,&percent) && percent==17);
    assert(!CpuStats_Update(&sample,tasks+1,1,10000,&percent));
    tasks[0].run_time=(1ULL<<32)+1000;
    assert(!CpuStats_Update(&sample,tasks,1,(1ULL<<32)+2000,&percent));
    tasks[0].run_time+=500;
    assert(CpuStats_Update(&sample,tasks,1,(1ULL<<32)+3000,&percent) && percent==50);
    assert(!CpuStats_Update(&sample,tasks,0,10000,&percent));
    puts("CPU stats: timer wrap, interval load, deleted tasks, invalid snapshots and 64-bit time passed");
    return 0;
}
