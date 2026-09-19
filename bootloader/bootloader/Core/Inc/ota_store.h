#ifndef OTA_STORE_H
#define OTA_STORE_H
#include <stdint.h>
#include <stdbool.h>
#include "ota_wire.h"

/* 由硬件适配层提供外部Flash访问，返回true表示完成。 */
bool OtaFlash_Read(uint32_t address, uint8_t *data, uint32_t size);
bool OtaFlash_Write(uint32_t address, const uint8_t *data, uint32_t size);
bool OtaFlash_Erase(uint32_t address);
void OtaFlash_Progress(uint32_t done, uint32_t total, uint32_t state);

/**
  * @brief 执行已通过帧CRC的请求；cmd/session/offset为请求头，data/size为载荷，next返回已接收偏移。
  * @retval OTA协议状态码；只操作外部下载槽和参数扇区。
  */
uint32_t OtaStore_Request(uint32_t cmd, uint32_t session, uint32_t offset,
                          const uint8_t *data, uint32_t size, uint32_t *next);
/** @brief 释放未完成的超时下载会话，不擦除持久化记录。 @retval 无。 */
void OtaStore_Expire(void);
#endif
