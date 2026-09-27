#include "boot_selftest.h"
#include "selftest_wire.h"
#include "fatfs.h"
#include "diskio.h"
#include "FreeRTOS.h"
#include "task.h"
#include <ctype.h>

extern SD_HandleTypeDef hsd1;
#define SELFTEST_BUDGET_MS 60000U
static SelfTestRecord records[SELFTEST_MAX_RECORDS];
static uint32_t record_count;
static bool report_ready;
static uint32_t started;

/** @brief 添加阶段记录，code为阶段结果，result为FatFs状态。 @retval 新记录指针。 */
static SelfTestRecord *record(uint32_t code,uint32_t result)
{
    SelfTestRecord *r=&records[record_count++];
    memset(r,0,sizeof(*r));r->code=code;r->result=result;
    r->hal=HAL_SD_GetError(&hsd1);r->elapsed=HAL_GetTick()-started;
    return r;
}
/** @brief 判断name是否为受支持的音频扩展名，不进行解码。 @retval true表示匹配。 */
static bool audio_name(const char *name)
{
    const char *ext=strrchr(name,'.');
    if(!ext || strlen(ext)!=4) return false;
    char lower[5]={'.',(char)tolower((unsigned char)ext[1]),(char)tolower((unsigned char)ext[2]),(char)tolower((unsigned char)ext[3]),0};
    return strcmp(lower,".wav")==0 || strcmp(lower,".mp3")==0 || strcmp(lower,".pcm")==0;
}
/** @brief 在临界区发布只读报告。 @retval 无。 */
static void publish(void)
{
    taskENTER_CRITICAL();report_ready=true;taskEXIT_CRITICAL();
}
/**
  * @brief 扫描根目录并完整读取音频，argument未使用；错误和超限不停止网关。
  * @retval 无，完成后退出任务。
  */
static void selftest_task(void *argument)
{
    (void)argument;started=HAL_GetTick();bool passed=true;uint32_t files=0;
    if(retSD || disk_initialize(0)!=0) {record(3,FR_NOT_READY);passed=false;goto done;}
    record(0,FR_OK);
    FRESULT fr=f_mount(&SDFatFS,SDPath,1);
    if(fr!=FR_OK) {record(4,fr);passed=false;goto unmount;}
    record(1,FR_OK);
    DIR dir;FILINFO info;FIL file;uint8_t buffer[4096];
    fr=f_opendir(&dir,"0:/");
    if(fr!=FR_OK) {record(5,fr);passed=false;goto unmount;}
    for(;;) {
        if((uint32_t)(HAL_GetTick()-started)>=SELFTEST_BUDGET_MS) {record(8,FR_TIMEOUT);passed=false;break;}
        if(record_count>=SELFTEST_MAX_RECORDS-2U) {record(9,FR_OK);passed=false;break;}
        fr=f_readdir(&dir,&info);
        if(fr!=FR_OK) {record(5,fr);passed=false;break;}
        if(!info.fname[0]) break;
        if((info.fattrib&AM_DIR) || !audio_name(info.fname)) continue;
        ++files;
        SelfTestRecord *r=record(2,FR_OK);
        memcpy(r->name,info.fname,strlen(info.fname)+1);
        r->size=(uint32_t)info.fsize;
        char path[SELFTEST_NAME_SIZE+4];snprintf(path,sizeof(path),"0:/%s",info.fname);
        uint32_t file_start=HAL_GetTick();
        fr=f_open(&file,path,FA_READ);
        if(fr==FR_OK) {
            for(;;) {
                if((uint32_t)(HAL_GetTick()-started)>=SELFTEST_BUDGET_MS) {r->code=8;fr=FR_TIMEOUT;break;}
                UINT got=0;fr=f_read(&file,buffer,sizeof(buffer),&got);
                r->read+=got;
                if(fr!=FR_OK || got<sizeof(buffer)) break;
                /* 每块让出CPU，不阻塞GUI、上传及看门狗任务。 */
                vTaskDelay(1U);
            }
            FRESULT close_result=f_close(&file);
            if(fr==FR_OK) fr=close_result;
        }
        if(fr!=FR_OK || r->read!=r->size) {if(r->code!=8)r->code=6;passed=false;}
        else if(!r->size) {r->code=13;passed=false;}
        r->result=fr;r->hal=HAL_SD_GetError(&hsd1);r->elapsed=HAL_GetTick()-file_start;
        if(r->code==8) break;
    }
    fr=f_closedir(&dir);
    if(fr!=FR_OK && record_count<SELFTEST_MAX_RECORDS-1U) {record(5,fr);passed=false;}
    if(!files && passed) {record(7,FR_NO_FILE);passed=false;}
unmount:
    (void)f_mount(NULL,SDPath,0);
done:
    record(passed?10:11,FR_OK)->size=files;
    publish();vTaskDelete(NULL);
}
/** @brief 启动低优先级自检任务，任务栈容纳长文件名和分块读缓冲区。 @retval 无。 */
void BootSelfTest_Start(void)
{
    if(xTaskCreate(selftest_task,"sdSelfTest",12288U/sizeof(StackType_t),
                   NULL,16U,NULL)!=pdPASS) {
        started=HAL_GetTick();record(12,FR_NOT_ENOUGH_CORE);record(11,FR_NOT_ENOUGH_CORE);publish();
    }
}
/**
  * @brief 由唯一串口发送任务逐条发送报告，确认CRC后推进，每30秒重放以恢复ESP重启后的报告。
  * @param uart 串口句柄；run报告编号；ack ESP已接收并暂存记录的CRC，不代表上位机已收到。
  * @retval 无。
  */
void BootSelfTest_Process(UART_HandleTypeDef *uart,uint32_t run,uint32_t ack)
{
    static uint32_t index,expected,next_send;
    static bool waiting;
    taskENTER_CRITICAL();bool ready=report_ready;taskEXIT_CRITICAL();
    if(!ready) return;
    uint32_t now=HAL_GetTick();
    if(waiting && ack==expected) {
        waiting=false;++index;next_send=now+20U;
        if(index==record_count) {index=0;next_send=now+30000U;}
    }
    if((int32_t)(now-next_send)<0) return;
    SelfTestRecord r=records[index];r.run=run;r.index=index;r.total=record_count;
    char line[SELFTEST_LINE_SIZE];
    size_t length=SelfTest_Encode(&r,line,&expected);
    waiting=length && HAL_UART_Transmit(uart,(uint8_t*)line,(uint16_t)length,100U)==HAL_OK;
    next_send=now+2000U;
}
