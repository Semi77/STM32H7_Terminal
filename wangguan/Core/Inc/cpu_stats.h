#ifndef CPU_STATS_H
#define CPU_STATS_H
#include <stdbool.h>
#include <stdint.h>
/** @brief 启动独占的TIM2统计时间源。 @retval 无。 */
void CpuStats_Init(void);
/** @brief 返回扩展为64位的累计微秒数。 @retval 累计运行时间。 */
uint64_t CpuStats_GetCounter(void);
#define CPU_STATS_NAME_MAX 16
/* 保存任务累计时间与调用方持有的采样基线。 */
typedef struct {
    char name[CPU_STATS_NAME_MAX];
    uint64_t run_time;
} CpuStatsTask;
typedef struct {
    uint64_t elapsed, idle;
    bool valid;
} CpuStatsSample;
/**
  * @brief 按sample保存的前次快照计算CPU负载，tasks/count为任务列表，elapsed为累计时间。
  * @param percent 有效时返回最近采样窗口的百分比。
  * @retval true表示有效，false表示首次采样或数据异常。
  */
bool CpuStats_Update(CpuStatsSample *sample, const CpuStatsTask *tasks,
                     uint32_t count, uint64_t elapsed, uint32_t *percent);
#endif
