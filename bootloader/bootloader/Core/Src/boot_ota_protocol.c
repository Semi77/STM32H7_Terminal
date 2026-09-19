#include "boot_ota.h"
#include "ota_install.h"
#include <stdio.h>

static bool busy;
static uint8_t frame[OTA_FRAME_MAX];
static uint32_t frame_used, frame_tick, session_tick;
#define OTA_SESSION_TIMEOUT_MS 60000U
#define OTA_FRAME_TIMEOUT_MS 250U

bool BootOta_Busy(void) { return busy || OtaInstall_Blocked(); }

/** @brief 超时释放未完成下载和残帧，已完成下载不自动取消。 @retval 无。 */
void BootOta_Poll(void)
{
    uint32_t now=HAL_GetTick();
    if (frame_used && (uint32_t)(now-frame_tick)>=OTA_FRAME_TIMEOUT_MS) frame_used=0;
    if (busy && (uint32_t)(now-session_tick)>=OTA_SESSION_TIMEOUT_MS) {
        OtaStore_Expire();
        busy=false;
        OtaFlash_Progress(0,0,4);
    }
}

/** @brief 接收溢出后丢弃残帧，下一完整帧重新同步。 @retval 无。 */
void BootOta_ResetReceiver(void) { frame_used=0; }

/**
  * @brief 非阻塞拼接二进制帧，uart用于回复，byte为环形缓冲取出的字节。
  * @retval true表示字节属于OTA协议，false表示交给文本命令解析。
  */
bool BootOta_Byte(UART_HandleTypeDef *uart, uint8_t byte)
{
    uint32_t now=HAL_GetTick();
    if (frame_used && (uint32_t)(now-frame_tick)>=OTA_FRAME_TIMEOUT_MS) frame_used=0;
    if (!frame_used && byte!='H') return false;
    frame_tick=now;
    frame[frame_used++]=byte;
    if (frame_used<=4 && frame[frame_used-1]!=(uint8_t)(OTA_MAGIC>>((frame_used-1)*8))) {
        frame_used=byte=='H'?1U:0U;
        frame[0]='H';
        return true;
    }
    if (frame_used<OTA_HEADER) return true;
    uint32_t n=ota_u32(frame+16);
    if (n>OTA_BLOCK) { frame_used=0; return true; }
    if (frame_used<OTA_HEADER+n+4) return true;
    frame_used=0;
    uint32_t cmd=ota_u32(frame+4), session=ota_u32(frame+8), offset=ota_u32(frame+12), next=0;
    uint32_t status=OTA_BAD_FRAME;
    if ((ota_crc_update(0xFFFFFFFFU,frame,OTA_HEADER+n)^0xFFFFFFFFU)==ota_u32(frame+OTA_HEADER+n)) {
        if (cmd==OTA_INSTALL) {
            if (!session || offset || n!=8) status=OTA_BAD_FRAME;
            else if (busy) status=OTA_BUSY;
            else {
                status=OtaInstall_Run(ota_u32(frame+OTA_HEADER),ota_u32(frame+OTA_HEADER+4));
                if (status==OTA_OK) next=ota_u32(frame+OTA_HEADER);
            }
        } else status=OtaStore_Request(cmd,session,offset,frame+OTA_HEADER,n,&next);
        if (status==OTA_OK) {
            session_tick=HAL_GetTick();
            if (cmd==OTA_BEGIN) busy=true;
            if (cmd==OTA_END || cmd==OTA_ABORT) busy=false;
        }
    }
    if (status!=OTA_OK) OtaFlash_Progress(next,0,5);
    uint8_t fields[20];
    ota_put(fields,session); ota_put(fields+4,cmd); ota_put(fields+8,offset);
    ota_put(fields+12,next); ota_put(fields+16,status);
    char reply[100];
    int len=snprintf(reply,sizeof(reply),"OTA:%08lX:%lu:%lu:%lu:%lu:%08lX\r\n",
        (unsigned long)session,(unsigned long)cmd,(unsigned long)offset,(unsigned long)next,
        (unsigned long)status,(unsigned long)(ota_crc_update(0xFFFFFFFFU,fields,20)^0xFFFFFFFFU));
    if (len>0 && len<(int)sizeof(reply)) (void)HAL_UART_Transmit(uart,(uint8_t*)reply,(uint16_t)len,100);
    if (cmd==OTA_INSTALL && status==OTA_OK) {
        /* 先发送最终校验确认，再复位恢复外设状态并启动新应用。 */
        HAL_Delay(1000);
        NVIC_SystemReset();
    }
    return true;
}
