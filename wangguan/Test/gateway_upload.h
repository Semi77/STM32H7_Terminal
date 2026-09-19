#ifndef GATEWAY_UPLOAD_H
#define GATEWAY_UPLOAD_H
#include "offline_cache.h"
#include "stm32h7xx_hal.h"
typedef enum { GATEWAY_OFFLINE, GATEWAY_RECOVERING, GATEWAY_ONLINE } GatewayState;
typedef struct {
  GatewayState state;
  uint32_t cached, overwritten, storage_errors, acknowledged, retries, dropped;
  HAL_StatusTypeDef tx_status;
} GatewayUploadStatus;
/**
  * @brief 绑定已初始化的huart并恢复Flash缓存，仅由上传任务调用。
  * @retval true表示缓存可用，false表示初始化失败。
  */
bool GatewayUpload_Init(UART_HandleTypeDef *huart);
/**
  * @brief 接收sample采样，在线空闲时暂存RAM，否则按顺序写入Flash。
  * @retval 无，失败计入存储错误与丢弃计数。
  */
void GatewayUpload_Submit(const GatewaySample *sample);
/**
  * @brief 推进上传状态机，online表示上传就绪，ack为平台确认序号且0表示无确认。
  * @retval 无，调用周期建议20毫秒。
  */
void GatewayUpload_Process(bool online, uint32_t ack);
/**
  * @brief 返回上传统计，仅允许上传任务读取后复制给其他任务。
  * @retval 当前状态及统计快照。
  */
GatewayUploadStatus GatewayUpload_GetStatus(void);
/**
  * @brief 将当前在途数据持久化，避免进入引导页时丢失待确认记录。
  * @retval true表示可安全复位，false表示存储失败。
  */
bool GatewayUpload_PrepareReset(void);
#endif
