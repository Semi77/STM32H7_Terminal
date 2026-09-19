#include "gateway_upload.h"
#include <stdio.h>
#include <string.h>

#define ACK_TIMEOUT_MS 5000U
#define RETRY_DELAY_MS 2000U
#define SEND_INTERVAL_MS 100U
static UART_HandleTypeDef *upload_uart;
static GatewaySample current;
static bool active, persisted, waiting, cache_ready, link_online;
static uint32_t sent_at, next_send;
static GatewayUploadStatus status;

/**
  * @brief 将当前RAM待确认记录保存到Flash，避免断线窗口丢失。
  * @retval true表示已保存或无需保存，false表示写失败。
  */
static bool persist_current(void)
{
  if (!active || persisted) return true;
  if (!cache_ready || !OfflineCache_Append(&current)) {
    ++status.storage_errors;
    return false;
  }
  persisted = true;
  return true;
}

bool GatewayUpload_Init(UART_HandleTypeDef *huart)
{
  upload_uart = huart;
  memset(&status, 0, sizeof(status));
  status.state = GATEWAY_OFFLINE;
  status.tx_status = HAL_BUSY;
  active = persisted = waiting = link_online = false;
  next_send = 0;
  cache_ready = OfflineCache_Init();
  if (!cache_ready) ++status.storage_errors;
  return cache_ready;
}

bool GatewayUpload_PrepareReset(void)
{
  return persist_current();
}

void GatewayUpload_Submit(const GatewaySample *sample)
{
  if (link_online && !active && !OfflineCache_Count()) {
    current = *sample;
    active = true;
    persisted = waiting = false;
  } else {
    if (!persist_current() || !cache_ready || !OfflineCache_Append(sample)) {
      ++status.storage_errors;
      ++status.dropped;
    }
  }
}

void GatewayUpload_Process(bool online, uint32_t ack)
{
  uint32_t now = HAL_GetTick();
  link_online = online;
  /* 扇区覆盖可能淘汰正在等待确认的旧记录，此时不得误删新的队首。 */
  if (active && persisted) {
    GatewaySample oldest;
    if (OfflineCache_Peek(&oldest) && oldest.sequence != current.sequence) {
      active = waiting = persisted = false;
    }
  }
  if (active && ack == current.sequence) {
    if (!persisted || OfflineCache_Ack(ack)) {
      active = waiting = persisted = false;
      ++status.acknowledged;
    } else ++status.storage_errors;
  }
  if (!online) {
    (void)persist_current();
    waiting = false;
    status.state = GATEWAY_OFFLINE;
    status.tx_status = HAL_BUSY;
    return;
  }
  status.state = OfflineCache_Count() ? GATEWAY_RECOVERING : GATEWAY_ONLINE;
  if (waiting) {
    if ((uint32_t)(now-sent_at) < ACK_TIMEOUT_MS) return;
    (void)persist_current();
    waiting = false;
    ++status.retries;
    next_send = now+RETRY_DELAY_MS;
  }
  if ((int32_t)(now-next_send) < 0) return;
  if (!active && OfflineCache_Count()) {
    if (!OfflineCache_Peek(&current)) { ++status.storage_errors; next_send=now+RETRY_DELAY_MS; return; }
    active = persisted = true;
  }
  if (!active) return;
  char line[160];
  int length = snprintf(line, sizeof(line),
    "{\"seq\":%lu,\"Temperature\":%lu,\"Humidity\":%lu,\"Brightness\":%lu}\r\n",
    (unsigned long)current.sequence, (unsigned long)current.temperature,
    (unsigned long)current.humidity, (unsigned long)current.brightness);
  status.tx_status = length > 0 && (size_t)length < sizeof(line) ?
    HAL_UART_Transmit(upload_uart, (uint8_t *)line, (uint16_t)length, 100U) : HAL_ERROR;
  if (status.tx_status == HAL_OK) {
    waiting = true;
    sent_at = now;
    next_send = now+SEND_INTERVAL_MS;
  } else {
    (void)persist_current();
    ++status.retries;
    next_send = now+RETRY_DELAY_MS;
  }
}

GatewayUploadStatus GatewayUpload_GetStatus(void)
{
  status.cached = OfflineCache_Count();
  status.overwritten = OfflineCache_Overwritten();
  status.state = !link_online ? GATEWAY_OFFLINE :
    (status.cached ? GATEWAY_RECOVERING : GATEWAY_ONLINE);
  return status;
}
