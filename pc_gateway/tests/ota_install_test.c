/* 使用真实安装器与NOR模拟器，在每个日志字节、擦除扇区和Flash字后注入断电。 */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "ota_install.h"
#include "memory_layout.h"

static uint8_t flash[0x800000], app[APP_FLASH_SIZE], image[APP_FLASH_SIZE];
static unsigned erases, programs, steps, cut;
static bool read_fail, write_fail, app_fail, corrupt_read, torn_word;
static jmp_buf power_off;

/** @brief 模拟当前持久化步骤后立即断电，cut为要中断的步骤编号。 */
static void checkpoint(void)
{
    if (++steps==cut) longjmp(power_off,1);
}

/** @brief 从外部Flash地址a读取n字节到p，可注入读取失败。 */
bool OtaFlash_Read(uint32_t a,uint8_t *p,uint32_t n)
{
    assert(a+n<=sizeof(flash));
    if (read_fail) return false;
    memcpy(p,flash+a,n); return true;
}

/** @brief 将p的n字节按NOR规则追加到日志地址a，每字节后允许断电。 */
bool OtaFlash_Write(uint32_t a,const uint8_t *p,uint32_t n)
{
    assert((a>=EXT_FW_SLOT1_BASE && a+n<=EXT_FW_SLOT1_BASE+EXT_FW_SLOT_SIZE) ||
           (a>=EXT_BOOT_PARA0_BASE && a+n<=EXT_RECOVERY_BASE+4096));
    if (write_fail) return false;
    for (uint32_t i=0;i<n;++i) { flash[a+i]&=p[i]; checkpoint(); }
    return true;
}

/** @brief 模拟恢复标记及下载区域擦除，a为扇区地址，断电可发生在半扇区。 */
bool OtaFlash_Erase(uint32_t a) {
    assert(a>=EXT_FW_SLOT1_BASE && a<EXT_RECOVERY_BASE+4096);
    memset(flash+a,255,2048); checkpoint();
    memset(flash+a+2048,255,2048); checkpoint();
    return true;
}

/** @brief 模拟屏幕阶段，done/total为进度，state为状态编号。 */
void OtaFlash_Progress(uint32_t done,uint32_t total,uint32_t state)
{ (void)done; (void)total; assert(state<=9); }

/** @brief 擦除四个内部应用扇区，每个扇区后注入断电。 */
bool OtaApp_Erase(void)
{
    ++erases;
    for (uint32_t i=0;i<APP_FLASH_SIZE;i+=0x20000) {
        memset(app+i,255,0x20000); checkpoint();
    }
    torn_word=false;
    return !app_fail;
}

/** @brief 模拟offset处的32字节Flash字编程，并在部分字写入后允许断电。 */
bool OtaApp_Write(uint32_t offset,const uint8_t *p,uint32_t n)
{
    assert(!(offset&31U) && n && offset+n<=sizeof(app));
    while (n) {
        uint32_t size=n>32?32:n;
        ++programs;
        torn_word=true;
        memcpy(app+offset,p,(size+1)/2); checkpoint();
        memcpy(app+offset,p,size); torn_word=false;
        offset+=32; p+=size; n-=size;
    }
    return !app_fail;
}

/** @brief 从完整内部镜像offset读取n字节到p，禁止访问断电留下的半个Flash字。
  * @retval true表示读回成功，可注入内容损坏以验证CRC拒绝启动。
  */
bool OtaApp_Read(uint32_t offset,uint8_t *p,uint32_t n)
{
    assert(!torn_word && offset+n<=sizeof(app));
    memcpy(p,app+offset,n);
    if (corrupt_read && offset==0 && n>16) p[16]^=1;
    return true;
}

/** @brief 初始化n字节已下载镜像及两份READY记录，内部区保留旧应用哨兵。 */
static uint32_t setup(uint32_t n)
{
    memset(flash,0x5A,sizeof(flash)); memset(app,0xC3,sizeof(app));
    memset(flash+EXT_BOOT_PARA0_BASE,255,12288);
    OtaStore_Expire();
    for (uint32_t i=0;i<n;++i) image[i]=(uint8_t)(i*37U);
    ota_put(image,0x24020000U); ota_put(image+4,APP_FLASH_BASE+9);
    memcpy(flash+EXT_FW_SLOT1_BASE,image,n);
    uint32_t crc=ota_crc_update(~0U,image,n)^~0U;
    uint8_t record[32];
    ota_put(record,0x314C444F); ota_put(record+4,1); ota_put(record+8,1);
    ota_put(record+12,EXT_FW_SLOT1_BASE); ota_put(record+16,n);
    ota_put(record+20,crc); ota_put(record+24,123);
    ota_put(record+28,ota_crc_update(~0U,record,28)^~0U);
    memcpy(flash+EXT_BOOT_PARA0_BASE,record,32); memcpy(flash+EXT_BOOT_PARA1_BASE,record,32);
    erases=programs=steps=cut=0;
    read_fail=write_fail=app_fail=corrupt_read=torn_word=false;
    return crc;
}

/** @brief 验证n字节安装结果、FF尾部和受保护区域均符合预期。 */
static void check(uint32_t n)
{
    assert(!OtaInstall_Blocked() && !memcmp(app,image,n));
    for (uint32_t i=n;i<sizeof(app);++i) assert(app[i]==255);
    for (uint32_t i=0;i<EXT_FW_SLOT1_BASE;++i) assert(flash[i]==0x5A);
    for (uint32_t i=EXT_CACHE_BASE;i<sizeof(flash);++i) assert(flash[i]==0x5A);
}

int main(void)
{
    uint32_t n=2051, crc=setup(n);
    assert(OtaInstall_Recover()==OTA_OK && !erases && app[0]==0xC3);
    assert(OtaInstall_Run(n,crc^1)==OTA_BAD_IMAGE && !erases);
    flash[EXT_FW_SLOT1_BASE+100]^=1;
    assert(OtaInstall_Run(n,crc)==OTA_CRC_ERROR && !erases);
    flash[EXT_FW_SLOT1_BASE+100]^=1;
    write_fail=true;
    assert(OtaInstall_Run(n,crc)==OTA_FLASH_ERROR && !erases);
    write_fail=false;
    assert(OtaInstall_Run(n,crc)==OTA_OK); check(n);
    unsigned total_steps=steps, old_erases=erases;
    assert(OtaInstall_Run(n,crc)==OTA_OK && erases==old_erases);
    assert(OtaInstall_Recover()==OTA_OK && erases==old_erases);

    /* 请求落盘到完成日志之间的每个断电点，都必须能恢复同一镜像。 */
    for (volatile unsigned point=1;point<=total_steps;++point) {
        crc=setup(n); cut=point;
        if (!setjmp(power_off)) { (void)OtaInstall_Run(n,crc); assert(0); }
        cut=0;
        assert(OtaInstall_Recover()==OTA_OK); check(n);
    }

    crc=setup(n); read_fail=true;
    assert(OtaInstall_Recover()==OTA_FLASH_ERROR && OtaInstall_Blocked());
    read_fail=false; assert(OtaInstall_Recover()==OTA_OK);
    app_fail=true;
    assert(OtaInstall_Run(n,crc)==OTA_FLASH_ERROR && OtaInstall_Blocked());
    app_fail=false; assert(OtaInstall_Recover()==OTA_OK); check(n);

    crc=setup(n); corrupt_read=true;
    assert(OtaInstall_Run(n,crc)==OTA_CRC_ERROR && OtaInstall_Blocked());
    assert(flash[EXT_BOOT_PARA0_BASE+64]==255);
    corrupt_read=false; assert(OtaInstall_Recover()==OTA_OK); check(n);

    crc=setup(n); app_fail=true;
    assert(OtaInstall_Run(n,crc)==OTA_FLASH_ERROR);
    app_fail=false; flash[EXT_FW_SLOT1_BASE+100]^=1;
    assert(OtaInstall_Recover()==OTA_CRC_ERROR && OtaInstall_Blocked());
    assert(erases==1);

    crc=setup(n); flash[EXT_BOOT_PARA0_BASE+28]^=1;
    assert(OtaInstall_Run(n,crc)==OTA_OK); check(n);
    crc=setup(n); flash[EXT_BOOT_PARA0_BASE+28]^=1; flash[EXT_BOOT_PARA1_BASE+28]^=1;
    assert(OtaInstall_Run(n,crc)==OTA_BAD_STATE && !erases);
    flash[EXT_BOOT_PARA0_BASE+32]=0;
    assert(OtaInstall_Recover()==OTA_BAD_STATE && OtaInstall_Blocked() && !erases);

    /* 已有DONE但内部损坏：仍从完整下载槽重装；修复期间断电不读取半写Flash字。 */
    crc=setup(n); assert(OtaInstall_Recover()==OTA_OK);
    assert(OtaInstall_Run(n,crc)==OTA_OK); app[100]^=1;
    old_erases=erases;
    assert(OtaInstall_Recover()==OTA_OK && erases==old_erases+1); check(n);
    for (volatile unsigned point=1;point<=total_steps;++point) {
        crc=setup(n); assert(OtaInstall_Run(n,crc)==OTA_OK);
        app[100]^=1; steps=0; cut=point;
        if (!setjmp(power_off)) (void)OtaInstall_Recover();
        cut=0; assert(OtaInstall_Recover()==OTA_OK); check(n);
    }
    /* 恢复源损坏后允许重新下载；参数擦除和下载过程中任意断电仍禁止启动。 */
    for (volatile unsigned point=1;point<=36;++point) {
        crc=setup(n); assert(OtaInstall_Run(n,crc)==OTA_OK);
        app[100]^=1; flash[EXT_FW_SLOT1_BASE+100]^=1;
        assert(OtaInstall_Recover()==OTA_CRC_ERROR && OtaInstall_Blocked());
        uint8_t request[16]; uint32_t next;
        ota_put(request,n); ota_put(request+4,crc); memcpy(request+8,image,8);
        steps=0; cut=point;
        if (!setjmp(power_off)) (void)OtaStore_Request(OTA_BEGIN,456,0,request,16,&next);
        cut=0; OtaStore_Expire();
        assert(OtaInstall_Recover()!=OTA_OK && OtaInstall_Blocked());
        assert(OtaStore_Request(OTA_BEGIN,789,0,request,16,&next)==OTA_OK);
        for (uint32_t pos=0;pos<n;pos+=OTA_BLOCK) {
            uint32_t len=n-pos; if(len>OTA_BLOCK)len=OTA_BLOCK;
            assert(OtaStore_Request(OTA_DATA,789,pos,image+pos,len,&next)==OTA_OK);
        }
        assert(OtaStore_Request(OTA_END,789,n,NULL,0,&next)==OTA_OK);
        assert(OtaInstall_Recover()==OTA_RECOVERY_REQUIRED && OtaInstall_Blocked());
        assert(OtaInstall_Run(n,crc)==OTA_OK); check(n);
    }

    n=APP_FLASH_SIZE; crc=setup(n);
    assert(OtaInstall_Run(n,crc)==OTA_OK); check(n);
    printf("OTA install: %u power cuts, source validation, readback CRC, metadata redundancy, retries and 512KiB passed\n",total_steps);
    return 0;
}
