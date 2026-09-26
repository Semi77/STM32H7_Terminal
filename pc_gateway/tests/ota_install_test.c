/* 用NOR模拟器验证A/B安装、试启动回滚和断电恢复。 */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "ota_install.h"
#include "memory_layout.h"

#define IMAGE_SIZE 2051U
static uint8_t external[0x800000], banks[2][APP_FLASH_SIZE], candidate[IMAGE_SIZE];
static uint32_t active, armed, confirmed;
static unsigned steps, cut, erases;
static bool boot_match, torn_word, read_fail, write_fail, select_fail;
static jmp_buf power_off;
/** @brief 在第cut次持久化操作后模拟断电。 */
static void checkpoint(void) { if (++steps==cut) longjmp(power_off,1); }
/** @brief 从外部Flash地址a读取n字节。 @retval 是否成功。 */
bool OtaFlash_Read(uint32_t a,uint8_t *p,uint32_t n)
{ assert(a+n<=sizeof(external)); if (read_fail) return false; memcpy(p,external+a,n); return true; }
/** @brief 按NOR规则逐字节写入外部Flash。 @retval 是否成功。 */
bool OtaFlash_Write(uint32_t a,const uint8_t *p,uint32_t n)
{
    assert((a==EXT_AB_STATE0_BASE || a==EXT_AB_STATE1_BASE) && n==64);
    if (write_fail) return false;
    for (uint32_t i=0;i<n;++i) { external[a+i]&=p[i]; checkpoint(); }
    return true;
}
/** @brief 分两步擦除日志扇区。 @retval true。 */
bool OtaFlash_Erase(uint32_t a)
{
    assert(a==EXT_AB_STATE0_BASE || a==EXT_AB_STATE1_BASE);
    memset(external+a,255,2048); checkpoint();
    memset(external+a+2048,255,2048); checkpoint(); return true;
}
/** @brief 检查安装进度状态值。 */
void OtaFlash_Progress(uint32_t done,uint32_t total,uint32_t state)
{ (void)done; (void)total; assert(state<=9); }
/** @brief 返回当前活动物理Bank。 @retval 0或1。 */
uint32_t OtaApp_CurrentBank(void) { return active; }
/** @brief 擦除非活动Bank应用区，每个扇区后允许断电。 @retval true。 */
bool OtaApp_Erase(void)
{
    ++erases;
    for (uint32_t i=0;i<APP_FLASH_SIZE;i+=0x20000U)
    { memset(banks[active^1U]+i,255,0x20000U); checkpoint(); }
    return true;
}
/** @brief 将n字节写入非活动Bank偏移offset，并允许半字断电。 @retval true。 */
bool OtaApp_Write(uint32_t offset,const uint8_t *p,uint32_t n)
{
    assert(!(offset&31U) && n && offset+n<=APP_FLASH_SIZE);
    while (n) {
        uint32_t size=n>32?32:n;
        torn_word=true;
        memcpy(banks[active^1U]+offset,p,(size+1)/2); checkpoint();
        memcpy(banks[active^1U]+offset,p,size); torn_word=false;
        offset+=32; p+=size; n-=size;
    }
    return true;
}
/** @brief 从指定物理Bank读取完整应用区，禁止读取半写区域。 @retval true。 */
bool OtaApp_ReadBank(uint32_t bank,uint32_t offset,uint8_t *p,uint32_t n)
{
    assert(bank<2 && offset+n<=APP_FLASH_SIZE);
    assert(!(torn_word && bank!=active));
    memcpy(p,banks[bank]+offset,n); return true;
}
/** @brief 模拟两份Bootloader是否一致。 @retval 是否一致。 */
bool OtaApp_BootCopyValid(void) { return boot_match; }
/** @brief 将目标物理Bank映射到低地址。 @retval true。 */
bool OtaApp_SelectBank(uint32_t bank)
{ assert(bank<2); if (select_fail) return false; active=bank; return true; }
/** @brief 返回模拟复位原因。 @retval 软件复位标志。 */
uint32_t OtaBoot_ResetReason(void) { return 1; }
/** @brief 把试启动令牌交给模拟应用。 */
void OtaBoot_ArmTrial(uint32_t token) { armed=token; }
/** @brief 判断模拟应用是否已确认。 @retval 是否确认。 */
bool OtaBoot_Confirmed(uint32_t token) { return confirmed && confirmed==token; }
/** @brief 清除试启动令牌。 */
void OtaBoot_ClearTrial(void) { armed=confirmed=0; }
/** @brief 模拟启动看门狗。 @retval true。 */
bool OtaBoot_StartWatchdog(void) { return true; }

/** @brief 准备工厂基线和下载完成的候选镜像。 @retval 候选CRC。 */
static uint32_t setup(void)
{
    memset(external,255,sizeof(external)); memset(banks,255,sizeof(banks));
    for (uint32_t i=0;i<APP_FLASH_SIZE;++i) banks[0][i]=(uint8_t)(i*23U);
    ota_put(banks[0],0x24020000U); ota_put(banks[0]+4,APP_FLASH_BASE+9U);
    for (uint32_t i=0;i<IMAGE_SIZE;++i) candidate[i]=(uint8_t)(i*37U);
    ota_put(candidate,0x24020000U); ota_put(candidate+4,APP_FLASH_BASE+9U);
    memcpy(external+EXT_FW_SLOT1_BASE,candidate,IMAGE_SIZE);
    uint32_t crc=ota_crc_update(~0U,candidate,IMAGE_SIZE)^~0U;
    uint8_t record[32];
    ota_put(record,0x314C444FU); ota_put(record+4,1); ota_put(record+8,1);
    ota_put(record+12,EXT_FW_SLOT1_BASE); ota_put(record+16,IMAGE_SIZE);
    ota_put(record+20,crc); ota_put(record+24,123);
    ota_put(record+28,ota_crc_update(~0U,record,28)^~0U);
    memcpy(external+EXT_BOOT_PARA0_BASE,record,32);
    memcpy(external+EXT_BOOT_PARA1_BASE,record,32);
    active=armed=confirmed=steps=cut=erases=0;
    boot_match=true; torn_word=read_fail=write_fail=select_fail=false;
    assert(OtaInstall_Recover()==OTA_OK && OtaInstall_PrepareDownload());
    return crc;
}
/** @brief 确认原Bank整片应用区未被安装过程修改。 */
static void baseline_intact(void)
{
    assert(ota_u32(banks[0])==0x24020000U);
    assert(ota_u32(banks[0]+4)==APP_FLASH_BASE+9U);
    for (uint32_t i=8;i<APP_FLASH_SIZE;++i) assert(banks[0][i]==(uint8_t)(i*23U));
}
/** @brief 模拟上电和可能发生的一次Bank切换。 @retval 最终恢复状态。 */
static uint32_t recover_settled(void)
{
    uint32_t result=OtaInstall_Recover();
    if (result==OTA_RECOVERY_REQUIRED) result=OtaInstall_Recover();
    return result;
}
int main(void)
{
    uint32_t crc=setup();
    assert(OtaInstall_Run(IMAGE_SIZE,crc^1U)==OTA_BAD_IMAGE && !erases);
    external[EXT_FW_SLOT1_BASE+100]^=1U;
    assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_CRC_ERROR && !erases);
    external[EXT_FW_SLOT1_BASE+100]^=1U;
    boot_match=false;
    assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_BAD_IMAGE && !erases);
    boot_match=true;
    steps=0;
    assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_OK && OtaInstall_Blocked());
    baseline_intact();
    unsigned total_steps=steps;
    assert(recover_settled()==OTA_OK && active==1 && OtaInstall_BeforeBoot());
    assert(armed); confirmed=armed;
    assert(OtaInstall_Recover()==OTA_OK && active==1 && !OtaInstall_Blocked());
    baseline_intact();
    /* 第二次升级从Bank2写回Bank1，检查物理Bank映射在交换后仍正确。 */
    candidate[100]^=2U;
    memcpy(external+EXT_FW_SLOT1_BASE,candidate,IMAGE_SIZE);
    crc=ota_crc_update(~0U,candidate,IMAGE_SIZE)^~0U;
    ota_put(external+EXT_BOOT_PARA0_BASE+20,crc);
    ota_put(external+EXT_BOOT_PARA0_BASE+28,
            ota_crc_update(~0U,external+EXT_BOOT_PARA0_BASE,28)^~0U);
    memcpy(external+EXT_BOOT_PARA1_BASE,external+EXT_BOOT_PARA0_BASE,32);
    assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_OK);
    assert(recover_settled()==OTA_OK && active==0 && OtaInstall_BeforeBoot());
    confirmed=armed;
    assert(OtaInstall_Recover()==OTA_OK && active==0 && !OtaInstall_Blocked());
    assert(!memcmp(banks[0],candidate,IMAGE_SIZE));

    crc=setup();
    assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_OK);
    assert(recover_settled()==OTA_OK && active==1);
    assert(OtaInstall_BeforeBoot() && armed);
    assert(OtaInstall_Recover()==OTA_OK && active==1);
    assert(OtaInstall_BeforeBoot() && armed);
    assert(recover_settled()==OTA_OK && active==0 && !OtaInstall_Blocked());
    assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_BAD_IMAGE && erases==1);
    baseline_intact();

    crc=setup(); assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_OK);
    select_fail=true;
    assert(OtaInstall_Recover()==OTA_OK && active==0 && !OtaInstall_Blocked());
    assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_BAD_IMAGE);
    baseline_intact();

    for (volatile unsigned point=1;point<=total_steps;++point) {
        crc=setup(); cut=point; steps=0;
        if (!setjmp(power_off)) { (void)OtaInstall_Run(IMAGE_SIZE,crc); assert(0); }
        cut=0;
        assert(recover_settled()==OTA_OK && !OtaInstall_Blocked());
        baseline_intact();
        assert(active==0 || (active==1 && !memcmp(banks[1],candidate,IMAGE_SIZE)));
        if (active==0) {
            assert(OtaInstall_PrepareDownload());
            assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_OK);
            assert(recover_settled()==OTA_OK && active==1);
        }
    }
    crc=setup(); write_fail=true;
    assert(OtaInstall_Run(IMAGE_SIZE,crc)==OTA_FLASH_ERROR && !erases);
    write_fail=false; assert(OtaInstall_Recover()==OTA_OK);
    read_fail=true;
    assert(OtaInstall_Recover()==OTA_FLASH_ERROR && OtaInstall_Blocked());
    read_fail=false; assert(OtaInstall_Recover()==OTA_OK);
    crc=setup();
    external[EXT_AB_STATE0_BASE+4]^=1U;
    external[EXT_AB_STATE1_BASE+4]=0U;
    assert(OtaInstall_Recover()==OTA_BAD_STATE && OtaInstall_Blocked());
    /* 先启动Boot后再经SWD烧应用，空基线必须允许补登记。 */
    memset(external,255,sizeof(external));
    memset(banks,255,sizeof(banks));
    active=armed=confirmed=cut=0;
    read_fail=write_fail=select_fail=false;
    assert(OtaInstall_Recover()==OTA_BAD_IMAGE && OtaInstall_Blocked());
    ota_put(banks[0],0x24020000U);
    ota_put(banks[0]+4,APP_FLASH_BASE+9U);
    assert(OtaInstall_Recover()==OTA_OK && !OtaInstall_Blocked());
    assert(OtaInstall_BeforeBoot());
    printf("A/B install: %u injected power cuts, confirmation, rollback and journal corruption passed\n",total_steps);
    return 0;
}
