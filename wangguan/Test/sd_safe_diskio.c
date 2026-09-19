/* 自定义SDMMC1磁盘驱动，替代CubeMX的sd_diskio.c。 */
#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "cmsis_os2.h"
#include <stdbool.h>
#include <string.h>
extern SD_HandleTypeDef hsd1;
#define SD_IO_TIMEOUT_MS 2000U
#define SD_BOUNCE_SIZE 4096U
static uint8_t bounce[SD_BOUNCE_SIZE] __attribute__((section(".sd_dma"), aligned(32)));
static osSemaphoreId_t completion;
static osMutexId_t io_lock;
static volatile DSTATUS disk_state=STA_NOINIT;

/** @brief 将毫秒ms向上取整为RTOS节拍。 @retval 等待节拍数。 */
static uint32_t ticks(uint32_t ms)
{
    return (uint32_t)(((uint64_t)ms*osKernelGetTickFreq()+999U)/1000U);
}
/** @brief 等待卡就绪并让出CPU。 @retval true表示可传输，false表示错误或超时。 */
static bool wait_ready(void)
{
    uint32_t start=HAL_GetTick();
    do {
        HAL_SD_CardStateTypeDef state=HAL_SD_GetCardState(&hsd1);
        if(state==HAL_SD_CARD_TRANSFER) return true;
        if(state==HAL_SD_CARD_ERROR || state==HAL_SD_CARD_DISCONNECTED) return false;
        osDelay(1);
    } while((uint32_t)(HAL_GetTick()-start)<SD_IO_TIMEOUT_MS);
    return false;
}
/** @brief 在调度器运行后初始化磁盘lun及同步对象。 @retval 失败返回未就绪，不停止网关。 */
static DSTATUS SD_initialize(BYTE lun)
{
    if(lun || osKernelGetState()!=osKernelRunning) return STA_NOINIT;
    if(!completion) completion=osSemaphoreNew(1,0,NULL);
    if(!io_lock) io_lock=osMutexNew(NULL);
    if(!completion || !io_lock) return STA_NOINIT;
    if(osMutexAcquire(io_lock,ticks(SD_IO_TIMEOUT_MS))!=osOK) return STA_NOINIT;
    disk_state=STA_NOINIT;
    if(BSP_SD_Init()==MSD_OK && wait_ready()) disk_state=0;
    osMutexRelease(io_lock);return disk_state;
}
/** @brief 返回磁盘lun最近一次操作状态。 @retval 磁盘状态。 */
static DSTATUS SD_status(BYTE lun) { return lun ? STA_NOINIT : disk_state; }

/**
  * @brief 使用AXI SRAM分块传输，write选择写入，buffer为普通内存，sector/count以512字节扇区计。
  * @retval 磁盘操作结果。
  */
static DRESULT transfer(bool write,BYTE *buffer,DWORD sector,UINT count)
{
    if(!buffer || !count) return RES_PARERR;
    if(disk_state || !io_lock) return RES_NOTRDY;
    if(osMutexAcquire(io_lock,ticks(SD_IO_TIMEOUT_MS))!=osOK) return RES_ERROR;
    DRESULT result=RES_OK;
    while(count) {
        UINT blocks=count>SD_BOUNCE_SIZE/512U?SD_BOUNCE_SIZE/512U:count;
        uint32_t bytes=blocks*512U;
        if(!wait_ready()) {result=RES_ERROR;break;}
        while(osSemaphoreAcquire(completion,0)==osOK) {}
        if(write) memcpy(bounce,buffer,bytes);
        if(SCB->CCR & SCB_CCR_DC_Msk) {
            if(write) SCB_CleanDCache_by_Addr((uint32_t*)bounce,(int32_t)bytes);
            else SCB_CleanInvalidateDCache_by_Addr((uint32_t*)bounce,(int32_t)bytes);
        }
        __DSB();
        HAL_StatusTypeDef status=write?HAL_SD_WriteBlocks_DMA(&hsd1,bounce,sector,blocks):HAL_SD_ReadBlocks_DMA(&hsd1,bounce,sector,blocks);
        if(status!=HAL_OK || osSemaphoreAcquire(completion,ticks(SD_IO_TIMEOUT_MS))!=osOK ||
           HAL_SD_GetError(&hsd1)!=HAL_SD_ERROR_NONE || !wait_ready()) {result=RES_ERROR;break;}
        if(!write) {
            if(SCB->CCR & SCB_CCR_DC_Msk) SCB_InvalidateDCache_by_Addr((uint32_t*)bounce,(int32_t)bytes);
            __DSB();memcpy(buffer,bounce,bytes);
        }
        buffer+=bytes;sector+=blocks;count-=blocks;
    }
    if(result!=RES_OK) {
        /* 同步终止DMA后再允许复用缓冲区。 */
        uint32_t error=HAL_SD_GetError(&hsd1);
        (void)HAL_SD_Abort(&hsd1);
        hsd1.ErrorCode=error?error:HAL_SD_ERROR_TIMEOUT;
        disk_state=STA_NOINIT;
    }
    osMutexRelease(io_lock);return result;
}
/** @brief 从磁盘lun的sector起读取count个扇区到buff。 @retval 读取结果。 */
static DRESULT SD_read(BYTE lun,BYTE *buff,DWORD sector,UINT count)
{ return lun?RES_PARERR:transfer(false,buff,sector,count); }
#if _USE_WRITE == 1
/** @brief 将buff写入磁盘lun的sector起count个扇区。 @retval 写入结果。 */
static DRESULT SD_write(BYTE lun,const BYTE *buff,DWORD sector,UINT count)
{ return lun?RES_PARERR:transfer(true,(BYTE*)buff,sector,count); }
#endif
/** @brief 查询磁盘lun参数或同步，cmd为命令，buff接收结果。 @retval 控制结果。 */
static DRESULT SD_ioctl(BYTE lun,BYTE cmd,void *buff)
{
    if(lun) return RES_PARERR;
    if(disk_state || !io_lock) return RES_NOTRDY;
    if(osMutexAcquire(io_lock,ticks(SD_IO_TIMEOUT_MS))!=osOK) return RES_ERROR;
    DRESULT result=RES_OK;HAL_SD_CardInfoTypeDef info;
    if(cmd==CTRL_SYNC) result=wait_ready()?RES_OK:RES_ERROR;
    else if(!buff || HAL_SD_GetCardInfo(&hsd1,&info)!=HAL_OK) result=RES_PARERR;
    else if(cmd==GET_SECTOR_COUNT) *(DWORD*)buff=info.LogBlockNbr;
    else if(cmd==GET_SECTOR_SIZE) *(WORD*)buff=512;
    else if(cmd==GET_BLOCK_SIZE) *(DWORD*)buff=1;
    else result=RES_PARERR;
    osMutexRelease(io_lock);return result;
}
const Diskio_drvTypeDef SD_Driver={SD_initialize,SD_status,SD_read,
#if _USE_WRITE == 1
    SD_write,
#endif
#if _USE_IOCTL == 1
    SD_ioctl
#endif
};
/** @brief DMA读完成时唤醒等待任务。 @retval 无。 */
void BSP_SD_ReadCpltCallback(void) {if(completion)osSemaphoreRelease(completion);}
/** @brief DMA写完成时唤醒等待任务。 @retval 无。 */
void BSP_SD_WriteCpltCallback(void) {if(completion)osSemaphoreRelease(completion);}
/** @brief SD错误时唤醒等待任务，hsd为产生错误的句柄。 @retval 无。 */
void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd) {if(hsd==&hsd1 && completion)osSemaphoreRelease(completion);}
