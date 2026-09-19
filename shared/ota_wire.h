#ifndef OTA_WIRE_H
#define OTA_WIRE_H
#include <stdint.h>
#include <stddef.h>

#define OTA_MAGIC 0x544F3748U /* 小端字节序：H7OT。 */
#define OTA_BLOCK 1024U
#define OTA_HEADER 20U
#define OTA_FRAME_MAX (OTA_HEADER + OTA_BLOCK + 4U)
enum { OTA_BEGIN=1, OTA_DATA, OTA_END, OTA_ABORT, OTA_STATUS, OTA_INSTALL };
enum { OTA_OK=0, OTA_BAD_FRAME, OTA_BAD_STATE, OTA_BAD_OFFSET,
       OTA_FLASH_ERROR, OTA_CRC_ERROR, OTA_BAD_IMAGE, OTA_BUSY, OTA_RECOVERY_REQUIRED };

/**
  * @brief 从p指向的四个小端字节读取无符号整数。
  * @retval 解码后的32位值。
  */
static inline uint32_t ota_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
/**
  * @brief 将数值v编码为p处的四个小端字节。
  * @retval 无。
  */
static inline void ota_put(uint8_t *p, uint32_t v)
{
    for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(v>>(i*8));
}
/**
  * @brief 续算CRC32内部状态；crc初始为全1，data为数据，n为字节数，最终结果异或全1。
  * @retval CRC-32/ISO-HDLC未执行最终异或的内部值。
  */
static inline uint32_t ota_crc_update(uint32_t crc, const uint8_t *data, size_t n)
{
    while (n--) {
        crc ^= *data++;
        for (unsigned i=0; i<8; ++i) crc=(crc>>1)^((0U-(crc&1U))&0xEDB88320U);
    }
    return crc;
}
#endif
