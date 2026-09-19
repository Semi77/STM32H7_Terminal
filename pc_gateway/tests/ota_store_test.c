/* 在电脑上使用内存模拟NOR Flash，执行真实下载状态机的边界和故障测试。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ota_store.h"
#include "memory_layout.h"

static uint8_t flash[0x800000], image[APP_FLASH_SIZE];
static unsigned writes, erases;
static bool fail_write;
/** @brief 正常下载测试无需持久化恢复保护。 @retval true。 */
bool OtaInstall_PrepareDownload(void) { return true; }

/** @brief 检查测试中的擦写范围仅落在下载槽和两份参数区；a/n为范围。 */
static void writable(uint32_t a,uint32_t n)
{
    assert((a>=EXT_FW_SLOT1_BASE && a+n<=EXT_FW_SLOT1_BASE+EXT_FW_SLOT_SIZE) ||
           (a>=EXT_BOOT_PARA0_BASE && a+n<=EXT_BOOT_PARA1_BASE+4096));
}
/** @brief 从模拟Flash地址a读n字节到p。 */
bool OtaFlash_Read(uint32_t a,uint8_t *p,uint32_t n)
{ assert(a+n<=sizeof(flash)); memcpy(p,flash+a,n); return true; }
/** @brief 按NOR只能由1写0的规则，将p的n字节写入a。 */
bool OtaFlash_Write(uint32_t a,const uint8_t *p,uint32_t n)
{
    writable(a,n); ++writes;
    if (fail_write) return false;
    for (uint32_t i=0;i<n;++i) flash[a+i]&=p[i];
    return true;
}
/** @brief 将a所在扇区恢复为全1并记录擦除次数。 */
bool OtaFlash_Erase(uint32_t a)
{ a&=~4095U; writable(a,4096); ++erases; memset(flash+a,255,4096); return true; }
/** @brief 测试不绘制屏幕，参数分别为进度、总长、状态。 */
void OtaFlash_Progress(uint32_t done,uint32_t total,uint32_t state)
{ (void)done; (void)total; (void)state; }

/** @brief 生成会话sid的BEGIN消息，n为镜像长度，crc为整包校验。 */
static uint32_t begin(uint32_t sid,uint32_t n,uint32_t crc,uint32_t *next)
{
    uint8_t data[16]; ota_put(data,n); ota_put(data+4,crc); memcpy(data+8,image,8);
    return OtaStore_Request(OTA_BEGIN,sid,0,data,16,next);
}

int main(void)
{
    assert((ota_crc_update(~0U,(const uint8_t*)"123456789",9)^~0U)==0xCBF43926U);
    memset(flash,0x5A,sizeof(flash));
    for (unsigned i=0;i<sizeof(image);++i) image[i]=(uint8_t)(i*37U);
    ota_put(image,0x24020000); ota_put(image+4,APP_FLASH_BASE+9);
    uint32_t next, n=2051, crc=ota_crc_update(~0U,image,n)^~0U;
    assert(begin(1,APP_FLASH_SIZE+1,crc,&next)==OTA_BAD_IMAGE);
    assert(begin(1,n,crc,&next)==OTA_OK && next==0);
    assert(begin(2,n,crc,&next)==OTA_BUSY);
    assert(begin(1,n,crc,&next)==OTA_OK && next==0);
    assert(OtaStore_Request(OTA_DATA,1,1024,image+1024,1024,&next)==OTA_BAD_OFFSET);
    assert(OtaStore_Request(OTA_DATA,1,0,image,1024,&next)==OTA_OK && next==1024);
    unsigned old_writes=writes, old_erases=erases;
    assert(OtaStore_Request(OTA_DATA,1,0,image,1024,&next)==OTA_OK && next==1024);
    assert(writes==old_writes && erases==old_erases);
    image[10]^=1;
    assert(OtaStore_Request(OTA_DATA,1,0,image,1024,&next)==OTA_CRC_ERROR);
    image[10]^=1;
    assert(OtaStore_Request(OTA_END,1,n,NULL,0,&next)==OTA_BAD_OFFSET);
    assert(flash[EXT_BOOT_PARA0_BASE]==255 && flash[EXT_BOOT_PARA1_BASE]==255);
    fail_write=true;
    assert(OtaStore_Request(OTA_DATA,1,1024,image+1024,1024,&next)==OTA_FLASH_ERROR && next==1024);
    fail_write=false;
    assert(OtaStore_Request(OTA_DATA,1,1024,image+1024,1024,&next)==OTA_OK);
    assert(OtaStore_Request(OTA_DATA,1,2048,image+2048,3,&next)==OTA_OK && next==n);
    flash[EXT_FW_SLOT1_BASE+100]^=1;
    assert(OtaStore_Request(OTA_END,1,n,NULL,0,&next)==OTA_CRC_ERROR);
    assert(flash[EXT_BOOT_PARA0_BASE]==255);
    flash[EXT_FW_SLOT1_BASE+100]^=1;
    fail_write=true;
    assert(OtaStore_Request(OTA_END,1,n,NULL,0,&next)==OTA_FLASH_ERROR);
    assert(flash[EXT_BOOT_PARA0_BASE]==255 && flash[EXT_BOOT_PARA1_BASE]==255);
    fail_write=false;
    assert(OtaStore_Request(OTA_END,1,n,NULL,0,&next)==OTA_OK);
    old_writes=writes;
    assert(OtaStore_Request(OTA_END,1,n,NULL,0,&next)==OTA_OK && writes==old_writes);
    assert(!memcmp(flash+EXT_FW_SLOT1_BASE,image,n));
    assert(!memcmp(flash+EXT_BOOT_PARA0_BASE,flash+EXT_BOOT_PARA1_BASE,32));
    assert(ota_u32(flash+EXT_BOOT_PARA0_BASE+28)==(ota_crc_update(~0U,flash+EXT_BOOT_PARA0_BASE,28)^~0U));
    assert(OtaStore_Request(OTA_ABORT,1,0,NULL,0,&next)==OTA_OK);
    assert(OtaStore_Request(OTA_DATA,1,0,image,1024,&next)==OTA_BAD_STATE);
    /* 最大固件覆盖所有下载扇区，绝不进入槽0或离线缓存。 */
    n=sizeof(image); crc=ota_crc_update(~0U,image,n)^~0U;
    assert(begin(3,n,crc,&next)==OTA_OK);
    for (uint32_t offset=0;offset<n;offset+=1024)
        assert(OtaStore_Request(OTA_DATA,3,offset,image+offset,1024,&next)==OTA_OK && next==offset+1024);
    assert(OtaStore_Request(OTA_END,3,n,NULL,0,&next)==OTA_OK);
    assert(!memcmp(flash+EXT_FW_SLOT1_BASE,image,n));
    for (unsigned i=0;i<EXT_FW_SLOT1_BASE;++i) assert(flash[i]==0x5A);
    for (unsigned i=EXT_CACHE_BASE;i<sizeof(flash);++i) assert(flash[i]==0x5A);
    puts("OTA store: CRC, bounds, duplicate ACK, offset, write fault, metadata and 384KiB passed");
    return 0;
}
