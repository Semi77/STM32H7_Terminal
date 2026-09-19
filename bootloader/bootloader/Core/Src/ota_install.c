#include "ota_install.h"
#include "memory_layout.h"
#include <string.h>

#define AB_MAGIC 0x32424148U
#define AB_MAX_ATTEMPTS 2U
enum { AB_STABLE, AB_WRITING, AB_TRIAL };
/* 固定64字节记录，最后4字节CRC，两扇区轮换提交。 */
typedef struct {
    uint32_t magic, version, sequence, phase;
    uint32_t good_bank, target_bank, attempts, good_size, good_crc;
    uint32_t target_size, target_crc, rejected_size, rejected_crc;
    uint32_t reset_reason, reserved, crc;
} AbState;
_Static_assert(sizeof(AbState)==64, "A/B journal size");
static AbState state;
static int state_copy;
static bool loaded, blocked=true;
static uint8_t buffer[OTA_BLOCK];

/** @brief 检查p的n字节是否全FF，识别工厂空白状态。 @retval 是否空白。 */
static bool blank(const uint8_t *p,uint32_t n)
{
    while (n--) if (*p++!=0xFFU) return false;
    return true;
}
/** @brief 校验记录s的格式、范围和CRC，拒绝残缺记录。 @retval 是否有效。 */
static bool valid(const AbState *s)
{
    return s->magic==AB_MAGIC && s->version==1 && s->sequence &&
        s->phase<=AB_TRIAL && s->good_bank<=1 && s->target_bank<=1 &&
        s->attempts<=AB_MAX_ATTEMPTS && s->good_size<=APP_FLASH_SIZE &&
        s->target_size<=APP_FLASH_SIZE &&
        (s->phase==AB_STABLE || (s->target_size>=8 && s->target_bank!=s->good_bank)) &&
        (ota_crc_update(~0U,(const uint8_t *)s,sizeof(*s)-4)^~0U)==s->crc;
}
/** @brief 擦写非当前副本并读回验证，next为新状态，成功才更新内存状态。 @retval 是否提交成功。 */
static bool commit(AbState next)
{
    if (state.sequence==0xFFFFFFFFU) return false;
    next.magic=AB_MAGIC; next.version=1; next.sequence=state.sequence+1;
    next.reserved=0;
    next.crc=ota_crc_update(~0U,(const uint8_t *)&next,sizeof(next)-4)^~0U;
    int copy=state_copy==0?1:0;
    uint32_t address=copy?EXT_AB_STATE1_BASE:EXT_AB_STATE0_BASE;
    AbState check;
    if (!OtaFlash_Erase(address) || !OtaFlash_Write(address,(const uint8_t *)&next,sizeof(next)) ||
        !OtaFlash_Read(address,(uint8_t *)&check,sizeof(check)) || memcmp(&next,&check,sizeof(next))) return false;
    state=next; state_copy=copy;
    return true;
}
/** @brief 校验data中的栈顶和入口，size为镜像长度，入口始终使用执行地址。 @retval 是否有效。 */
static bool vector_valid(const uint8_t *data,uint32_t size)
{
    uint32_t sp=ota_u32(data),pc=ota_u32(data+4);
    return size>=8 && size<=APP_FLASH_SIZE && !(sp&7U) &&
        ((sp>0x20000000U && sp<=0x20020000U) || (sp>0x24000000U && sp<=0x24080000U)) &&
        (pc&1U) && (pc&~1U)>=APP_FLASH_BASE && (pc&~1U)<APP_FLASH_BASE+size;
}
/** @brief 对bank镜像计算CRC，bank为2表示外部槽，size为长度，crc返回结果。 @retval OTA状态码。 */
static uint32_t checksum(uint32_t bank,uint32_t size,uint32_t *crc)
{
    if (size<8 || size>APP_FLASH_SIZE) return OTA_BAD_IMAGE;
    *crc=~0U;
    for (uint32_t pos=0;pos<size;pos+=OTA_BLOCK) {
        uint32_t n=size-pos; if (n>OTA_BLOCK) n=OTA_BLOCK;
        if (!(bank==2?OtaFlash_Read(EXT_FW_SLOT1_BASE+pos,buffer,n):OtaApp_ReadBank(bank,pos,buffer,n)))
            return OTA_FLASH_ERROR;
        if (!pos && !vector_valid(buffer,size)) return OTA_BAD_IMAGE;
        *crc=ota_crc_update(*crc,buffer,n);
        OtaFlash_Progress(pos+n,size,bank==2?2:8);
    }
    *crc^=~0U;
    return OTA_OK;
}
/** @brief 验证bank的size字节镜像与expected完整CRC。 @retval OTA状态码。 */
static uint32_t verify(uint32_t bank,uint32_t size,uint32_t expected)
{
    uint32_t crc,status=checksum(bank,size,&crc);
    return status==OTA_OK && crc!=expected?OTA_CRC_ERROR:status;
}
/** @brief 拒绝候选并持久化回到旧版本的决定，避免无限试启动。 @retval OTA状态码。 */
static uint32_t reject(void)
{
    AbState next=state;
    next.phase=AB_STABLE; next.rejected_size=state.target_size; next.rejected_crc=state.target_crc;
    next.attempts=0;
    return commit(next)?OTA_OK:OTA_FLASH_ERROR;
}
uint32_t OtaInstall_Recover(void)
{
    AbState copies[2];
    blocked=true; loaded=false; state_copy=-1; memset(&state,0,sizeof(state));
    for (unsigned i=0;i<2;++i)
        if (!OtaFlash_Read(i?EXT_AB_STATE1_BASE:EXT_AB_STATE0_BASE,(uint8_t *)&copies[i],sizeof(AbState)))
            return OTA_FLASH_ERROR;
    for (unsigned i=0;i<2;++i) if (valid(&copies[i])) {
        if (state_copy>=0 && copies[i].sequence==state.sequence && memcmp(&copies[i],&state,sizeof(state)))
            return OTA_BAD_STATE;
        if (state_copy<0 || copies[i].sequence>state.sequence) { state=copies[i]; state_copy=(int)i; }
    }
    uint32_t current=OtaApp_CurrentBank(),status;
    if (state_copy<0) {
        /* 仅工厂空白状态允许登记SWD烧入的可信基线，损坏日志需维护清理。 */
        if (!blank((uint8_t *)copies,sizeof(copies))) return OTA_BAD_STATE;
        state.good_bank=current; state.target_bank=current^1U;
        uint32_t crc;
        status=checksum(current,APP_FLASH_SIZE,&crc);
        if (status==OTA_OK) { state.good_size=APP_FLASH_SIZE; state.good_crc=crc; }
        else if (status!=OTA_BAD_IMAGE) return status;
        if (!commit(state)) return OTA_FLASH_ERROR;
    }
    loaded=true;
    if (state.phase==AB_WRITING) {
        /* 不读取半写Flash，保留原版，重新安装时完整擦除候选区。 */
        AbState next=state; next.phase=AB_STABLE;
        if (!commit(next)) return OTA_FLASH_ERROR;
    }
    if (state.phase==AB_TRIAL) {
        status=verify(state.target_bank,state.target_size,state.target_crc);
        if (status!=OTA_OK) {
            if (reject()!=OTA_OK) return OTA_FLASH_ERROR;
        } else if (current==state.target_bank && state.attempts && OtaBoot_Confirmed(state.sequence)) {
            AbState next=state;
            next.phase=AB_STABLE; next.good_bank=state.target_bank;
            next.good_size=state.target_size; next.good_crc=state.target_crc; next.attempts=0;
            if (!commit(next)) return OTA_FLASH_ERROR;
        } else if (state.attempts>=AB_MAX_ATTEMPTS) {
            if (reject()!=OTA_OK) return OTA_FLASH_ERROR;
        }
    }
    OtaBoot_ClearTrial();
    uint32_t desired=state.phase==AB_TRIAL?state.target_bank:state.good_bank;
    if (state.phase==AB_STABLE) {
        status=verify(desired,state.good_size,state.good_crc);
        if (status!=OTA_OK) return status;
    }
    if (desired!=current) {
        if (!OtaApp_BootCopyValid() || !OtaApp_SelectBank(desired)) return OTA_FLASH_ERROR;
        return OTA_RECOVERY_REQUIRED; /* 硬件成功时已复位，模拟器可返回。 */
    }
    blocked=false;
    return OTA_OK;
}
bool OtaInstall_BeforeBoot(void)
{
    if (blocked || !loaded) return false;
    if (state.phase==AB_TRIAL) {
        if (state.target_bank!=OtaApp_CurrentBank() || state.attempts>=AB_MAX_ATTEMPTS) return false;
        AbState next=state; ++next.attempts; next.reset_reason=OtaBoot_ResetReason();
        if (!commit(next)) { blocked=true; return false; }
        OtaBoot_ArmTrial(state.sequence);
    } else OtaBoot_ClearTrial();
    return OtaBoot_StartWatchdog();
}
uint32_t OtaInstall_Run(uint32_t size,uint32_t crc)
{
    if (!loaded || state.phase!=AB_STABLE) return OTA_BAD_STATE;
    if (state.rejected_size==size && state.rejected_crc==crc) return OTA_BAD_IMAGE;
    uint8_t record[32]; bool found=false;
    for (unsigned i=0;i<2;++i) {
        if (!OtaFlash_Read(i?EXT_BOOT_PARA1_BASE:EXT_BOOT_PARA0_BASE,record,sizeof(record))) return OTA_FLASH_ERROR;
        bool ok=ota_u32(record)==0x314C444FU && ota_u32(record+4)==1 && ota_u32(record+8)==1 &&
            ota_u32(record+12)==EXT_FW_SLOT1_BASE && ota_u32(record+16)==size && ota_u32(record+20)==crc &&
            ota_u32(record+24) && (ota_crc_update(~0U,record,28)^~0U)==ota_u32(record+28);
        found=found||ok;
    }
    if (!found) return OTA_BAD_IMAGE;
    uint32_t status=verify(2,size,crc);
    if (status!=OTA_OK) return status;
    if (!OtaApp_BootCopyValid()) return OTA_BAD_IMAGE;
    /* 只从原已确认Bank安装，绝不擦除唯一可回滚镜像。 */
    if (OtaApp_CurrentBank()!=state.good_bank) return OTA_BAD_STATE;
    AbState next=state;
    next.phase=AB_WRITING; next.target_bank=OtaApp_CurrentBank()^1U;
    next.target_size=size; next.target_crc=crc; next.attempts=0;
    if (!commit(next)) return OTA_FLASH_ERROR;
    OtaFlash_Progress(0,size,6);
    if (!OtaApp_Erase()) return OTA_FLASH_ERROR;
    for (uint32_t pos=0;pos<size;pos+=OTA_BLOCK) {
        uint32_t n=size-pos; if (n>OTA_BLOCK) n=OTA_BLOCK;
        if (!OtaFlash_Read(EXT_FW_SLOT1_BASE+pos,buffer,n) || !OtaApp_Write(pos,buffer,n)) return OTA_FLASH_ERROR;
        OtaFlash_Progress(pos+n,size,7);
    }
    status=verify(state.target_bank,size,crc);
    if (status!=OTA_OK) return status;
    next=state; next.phase=AB_TRIAL;
    if (!commit(next)) return OTA_FLASH_ERROR;
    blocked=true;
    OtaFlash_Progress(size,size,9);
    return OTA_OK;
}
bool OtaInstall_Blocked(void) { return blocked; }
bool OtaInstall_PrepareDownload(void) { return loaded && state.phase==AB_STABLE; }
