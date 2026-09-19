#include "ota_store.h"
#include "ota_install.h"
#include "memory_layout.h"
#include <string.h>

static uint32_t session_id, length, expected_crc, received;
static bool active, ready;
static uint8_t verify[OTA_BLOCK];

/**
  * @brief 校验固件向量表；data为至少8字节镜像头，size为完整镜像长度。
  * @retval true表示栈顶及Thumb入口属于当前H743应用布局。
  */
static bool image_valid(const uint8_t *data, uint32_t size)
{
    uint32_t sp=ota_u32(data), pc=ota_u32(data+4);
    bool ram=(sp>0x20000000U && sp<=0x20020000U) || (sp>0x24000000U && sp<=0x24080000U);
    return ram && !(sp&7U) && (pc&1U) && (pc&~1U)>=APP_FLASH_BASE &&
           (pc&~1U)<APP_FLASH_BASE+size;
}

/**
  * @brief 清除两个下载参数记录，保证后续覆盖数据前不残留READY标记。
  * @retval true表示两个扇区均已擦除。
  */
static bool invalidate(void)
{
    return OtaFlash_Erase(EXT_BOOT_PARA0_BASE) && OtaFlash_Erase(EXT_BOOT_PARA1_BASE);
}

/**
  * @brief 读回整包校验并最后写入带记录CRC的两个READY副本。
  * @retval OTA_OK表示数据和两个参数记录均已读回验证。
  */
static uint32_t finish(void)
{
    uint32_t crc=0xFFFFFFFFU;
    OtaFlash_Progress(received,length,2);
    for (uint32_t pos=0; pos<length; pos+=OTA_BLOCK) {
        uint32_t n=length-pos; if (n>OTA_BLOCK) n=OTA_BLOCK;
        if (!OtaFlash_Read(EXT_FW_SLOT1_BASE+pos,verify,n)) return OTA_FLASH_ERROR;
        crc=ota_crc_update(crc,verify,n);
    }
    if ((crc^0xFFFFFFFFU)!=expected_crc) return OTA_CRC_ERROR;
    /* 32字节元数据：魔数、格式、状态、槽地址、长度、镜像CRC、会话、记录CRC。 */
    uint8_t record[32];
    ota_put(record,0x314C444FU); ota_put(record+4,1); ota_put(record+8,1);
    ota_put(record+12,EXT_FW_SLOT1_BASE); ota_put(record+16,length);
    ota_put(record+20,expected_crc); ota_put(record+24,session_id);
    ota_put(record+28,ota_crc_update(0xFFFFFFFFU,record,28)^0xFFFFFFFFU);
    for (unsigned i=0;i<2;++i) {
        uint32_t address=i?EXT_BOOT_PARA1_BASE:EXT_BOOT_PARA0_BASE;
        if (!OtaFlash_Write(address,record,sizeof(record)) ||
            !OtaFlash_Read(address,verify,sizeof(record)) || memcmp(record,verify,sizeof(record)))
            return OTA_FLASH_ERROR;
    }
    ready=true;
    OtaFlash_Progress(length,length,3);
    return OTA_OK;
}

uint32_t OtaStore_Request(uint32_t cmd, uint32_t session, uint32_t offset,
                          const uint8_t *data, uint32_t size, uint32_t *next)
{
    *next=received;
    if (!session || size>OTA_BLOCK) return OTA_BAD_FRAME;
    if (cmd==OTA_BEGIN) {
        if (size!=16 || offset) return OTA_BAD_FRAME;
        uint32_t total=ota_u32(data), crc=ota_u32(data+4);
        if (total<8 || total>APP_FLASH_SIZE || !image_valid(data+8,total)) return OTA_BAD_IMAGE;
        if (session==session_id && active) {
            return total==length && crc==expected_crc ? OTA_OK : OTA_BAD_STATE;
        }
        if (active && !ready) return OTA_BUSY;
        if (!OtaInstall_PrepareDownload()) return OTA_FLASH_ERROR;
        active=false; ready=false;
        OtaFlash_Progress(0,total,0);
        if (!invalidate()) return OTA_FLASH_ERROR;
        session_id=session; length=total; expected_crc=crc; received=0;
        active=true; *next=0;
        return OTA_OK;
    }
    if (!active || session!=session_id) return OTA_BAD_STATE;
    if (cmd==OTA_ABORT && !size && !offset) {
        if (!invalidate()) return OTA_FLASH_ERROR;
        active=false; ready=false; received=0; *next=0;
        OtaFlash_Progress(0,length,4);
        return OTA_OK;
    }
    if (cmd==OTA_STATUS && !size) return OTA_OK;
    if (cmd==OTA_END && !size && offset==length) {
        if (received!=length) return OTA_BAD_OFFSET;
        if (ready) return OTA_OK;
        uint32_t status=finish();
        if (status!=OTA_OK) { OtaFlash_Progress(received,length,5); }
        return status;
    }
    if (cmd!=OTA_DATA || !size || offset%OTA_BLOCK || offset>=length ||
        size!=((length-offset)>OTA_BLOCK?OTA_BLOCK:(length-offset))) return OTA_BAD_FRAME;
    if (offset<received && size<=received-offset) {
        return OtaFlash_Read(EXT_FW_SLOT1_BASE+offset,verify,size) && !memcmp(data,verify,size)
            ? OTA_OK : OTA_CRC_ERROR;
    }
    if (ready || offset!=received) return OTA_BAD_OFFSET;
    if (!offset && !image_valid(data,length)) return OTA_BAD_IMAGE;
    uint32_t address=EXT_FW_SLOT1_BASE+offset;
    if (!(offset&4095U) && !OtaFlash_Erase(address)) return OTA_FLASH_ERROR;
    if (!OtaFlash_Write(address,data,size) || !OtaFlash_Read(address,verify,size) || memcmp(data,verify,size))
        return OTA_FLASH_ERROR;
    received+=size; *next=received;
    OtaFlash_Progress(received,length,1);
    return OTA_OK;
}

/** @brief 释放超时会话的内存状态，保留已验证镜像及持久化恢复保护。 @retval 无。 */
void OtaStore_Expire(void)
{
    active=false; ready=false; received=0;
}
