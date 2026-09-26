#ifndef OFFLINE_CACHE_H
#define OFFLINE_CACHE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t sequence;
  uint32_t temperature;
  uint32_t humidity;
  uint32_t brightness;
  uint32_t tick_ms;
} GatewaySample;

/**
  * @brief 缓存扫描进度：0表示尚未扫描，UINT32_MAX表示结束，其余值为已扫描扇区数。
  */
extern volatile uint32_t g_offline_cache_scan_sectors;

/**
  * @brief 扫描后4MB缓存并预留不重复的序号区间，仅由上传任务调用。
  * @retval true表示恢复成功，false表示存储故障。
  */
bool OfflineCache_Init(void);
/**
  * @brief 分配跨重启不重复的序号，sequence为输出指针。
  * @retval true表示成功，false表示Flash故障或序号耗尽。
  */
bool OfflineCache_NextSequence(uint32_t *sequence);
/**
  * @brief 追加sample记录，写满时按扇区覆盖最旧数据。
  * @retval true表示提交成功，false表示写入失败。
  */
bool OfflineCache_Append(const GatewaySample *sample);
/**
  * @brief 读取最旧待上传记录到sample，不删除记录。
  * @retval true表示取得记录，false表示无数据或读取失败。
  */
bool OfflineCache_Peek(GatewaySample *sample);
/**
  * @brief 仅将队首序号等于sequence的记录标记为已确认。
  * @retval true表示成功，false表示不匹配或存储错误。
  */
bool OfflineCache_Ack(uint32_t sequence);
/**
  * @brief 返回当前待上传记录数量。
  * @retval 待上传数量。
  */
uint32_t OfflineCache_Count(void);
/**
  * @brief 返回本次启动以来因覆盖而丢弃的记录数量。
  * @retval 覆盖数量。
  */
uint32_t OfflineCache_Overwritten(void);
#endif
