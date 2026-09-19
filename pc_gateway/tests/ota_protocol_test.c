#include "boot_ota.h"
#include "ota_install.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t tick, requests, expires, resets;
static bool blocked;
static char reply[100];
static UART_HandleTypeDef uart;
/** @brief 返回模拟毫秒时间。 @retval 当前时间。 */
uint32_t HAL_GetTick(void) { return tick; }
/** @brief 保存真实协议层的回复，data/size为串口输出。 @retval 0。 */
int HAL_UART_Transmit(UART_HandleTypeDef *u,const uint8_t *data,uint16_t size,uint32_t timeout)
{ (void)u;(void)timeout; assert(size<sizeof(reply)); memcpy(reply,data,size);reply[size]=0;return 0; }
/** @brief 累加模拟时间delay。 @retval 无。 */
void HAL_Delay(uint32_t delay) { tick+=delay; }
/** @brief 记录协议层复位请求。 @retval 无。 */
void NVIC_SystemReset(void) { ++resets; }
/** @brief 返回模拟的持久化启动保护。 @retval 是否阻止启动。 */
bool OtaInstall_Blocked(void) { return blocked; }
/** @brief 模拟安装完成，size/crc为请求参数。 @retval OTA_OK。 */
uint32_t OtaInstall_Run(uint32_t size,uint32_t crc) { (void)size;(void)crc;blocked=false;return OTA_OK; }
/** @brief 检查完整CRC帧才会到达下载器，next返回下一偏移。 @retval OTA_OK。 */
uint32_t OtaStore_Request(uint32_t cmd,uint32_t session,uint32_t offset,const uint8_t *data,uint32_t size,uint32_t *next)
{ (void)cmd;(void)session;(void)data; ++requests;*next=offset+size;return OTA_OK; }
/** @brief 记录超时释放，不能解除持久化启动保护。 @retval 无。 */
void OtaStore_Expire(void) { ++expires; }
/** @brief 忽略测试中的显示，参数为进度及阶段。 @retval 无。 */
void OtaFlash_Progress(uint32_t done,uint32_t total,uint32_t state) { (void)done;(void)total;(void)state; }
/** @brief 生成包含二进制换行的cmd请求，n为载荷长度，out为输出帧。 @retval 总帧长。 */
static uint32_t make_frame(uint8_t *out,uint32_t cmd,uint32_t n)
{
    ota_put(out,OTA_MAGIC);ota_put(out+4,cmd);ota_put(out+8,123);ota_put(out+12,0);ota_put(out+16,n);
    for(uint32_t i=0;i<n;++i)out[20+i]=(uint8_t)i;
    ota_put(out+20+n,ota_crc_update(~0U,out,20+n)^~0U);
    return n+24;
}
/** @brief 逐字节送入真实接收器，data/n为任意拆分片段。 @retval 无。 */
static void feed(const uint8_t *data,uint32_t n) { while(n--) (void)BootOta_Byte(&uart,*data++); }
/** @brief 验证拆帧、坏帧、超时重同步、恢复下载及毫秒回绕。 @retval 0表示通过。 */
int main(void)
{
    uint8_t frame[OTA_FRAME_MAX];uint32_t n=make_frame(frame,OTA_BEGIN,16);
    feed(frame,10);assert(!requests);feed(frame+10,n-10);assert(requests==1 && BootOta_Busy());
    tick=59999;BootOta_Poll();assert(!expires);tick=60000;BootOta_Poll();assert(expires==1 && !BootOta_Busy());
    n=make_frame(frame,OTA_DATA,OTA_BLOCK);frame[n-1]^=1;feed(frame,n);assert(requests==1);
    frame[n-1]^=1;feed(frame,n);assert(requests==2);
    feed(frame,7);tick+=251;BootOta_Poll();feed(frame,n);assert(requests==3);
    /* 错误魔数和超长帧头之后的正确帧仍可处理。 */
    feed((const uint8_t *)"Hxx",3);ota_put(frame+16,OTA_BLOCK+1);feed(frame,20);
    n=make_frame(frame,OTA_BEGIN,16);blocked=true;feed(frame,n);assert(requests==4);
    tick+=60000;BootOta_Poll();assert(expires==2 && BootOta_Busy());
    blocked=false;tick=UINT32_MAX-100;feed(frame,n);tick+=60000;BootOta_Poll();assert(expires==3);
    feed(frame,n);n=make_frame(frame,OTA_END,0);feed(frame,n);tick+=120000;BootOta_Poll();assert(expires==3 && !BootOta_Busy());
    n=make_frame(frame,OTA_INSTALL,8);feed(frame,n);assert(resets==1);
    puts("OTA protocol: fragmentation, CRC, resync, timeout, recovery upload and tick wrap passed");
    return 0;
}
