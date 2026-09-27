#ifndef USART3_TEST_H
#define USART3_TEST_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include "gateway_upload.h"

typedef struct {
  uint32_t sequence;
  uint32_t temperature_c;
  uint32_t humidity_percent;
  uint32_t light_lux;
  HAL_StatusTypeDef tx_status;
  GatewayUploadStatus upload;
} Usart3TestSnapshot_t;

/**
  * @brief 获取最近一次模拟数据及网关上传统计的线程安全快照。
  * @param snapshot 用于接收快照的非空输出指针。
  * @retval true表示已有发送结果，false表示尚未发送或参数无效。
  */
bool Usart3Test_GetSnapshot(Usart3TestSnapshot_t *snapshot);

/**
  * @brief 创建每秒采样并按网络状态发送、缓存和补传的网关任务。
  * @param huart 已完成初始化的USART3句柄。
  * @retval HAL_OK表示任务创建成功，HAL_ERROR表示参数或任务创建失败。
  */
HAL_StatusTypeDef Usart3Test_Start(UART_HandleTypeDef *huart);

/**
  * @brief 查询C3联网状态，超过3秒未收到有效心跳则返回离线。
  * @retval true表示ESP32上传就绪且心跳未超时。
  */
bool Usart3Test_IsOnline(void);

/**
  * @brief 获取ESP32最近一次网络校准的UTC秒数和接收时刻。
  * @param utc_seconds 输出自1970年以来的UTC秒数。
  * @param received_tick_ms 输出STM32接收时的毫秒计数。
  * @retval true表示已有有效网络时间，false表示尚未校时或参数无效。
  */
bool Usart3Test_GetNetworkTime(uint32_t *utc_seconds, uint32_t *received_tick_ms);

/**
  * @brief 获取最近一次有效上位机页面指令的接收时刻。
  * @param tick_ms 接收时刻的输出指针，单位毫秒。
  * @retval true表示已收到页面指令，false表示尚未收到或参数无效。
  */
bool Usart3Test_GetUserCommandTick(uint32_t *tick_ms);

#endif
