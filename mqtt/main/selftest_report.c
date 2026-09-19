#include "selftest_report.h"
#include "selftest_wire.h"
#include "h7_uart.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>

typedef struct {
    SelfTestRecord records[SELFTEST_MAX_RECORDS];
    uint64_t mask;
    uint32_t run,total;
} Report;
static Report latest;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;

/** @brief 校验串口自检报告并保存后回复CRC确认，不依赖Wi-Fi或MQTT。 @retval 是否为自检消息。 */
bool selftest_report_accept(const char *line)
{
    if(strncmp(line,"SELFTEST:",9)!=0) return false;
    SelfTestRecord r;uint32_t crc;
    if(!SelfTest_Decode(line,&r,&crc)) return true;
    portENTER_CRITICAL(&lock);
    if(latest.run!=r.run || latest.total!=r.total) {
        latest.mask=0;latest.run=r.run;latest.total=r.total;
    }
    latest.records[r.index]=r;latest.mask|=UINT64_C(1)<<r.index;
    portEXIT_CRITICAL(&lock);
    char ack[24];int n=snprintf(ack,sizeof(ack),"ST_ACK:%08lX\r\n",(unsigned long)crc);
    uart_write_bytes(H7_UART_PORT,ack,n);
    return true;
}
/** @brief 在锁外构造JSON，文件名以GBK字节十六进制传输供上位机解码。 @retval 快照JSON或NULL。 */
cJSON *selftest_report_json(void)
{
    Report *copy=malloc(sizeof(*copy));
    if(!copy) return NULL;
    portENTER_CRITICAL(&lock);*copy=latest;portEXIT_CRITICAL(&lock);
    cJSON *root=cJSON_CreateObject();
    if(!root) {free(copy);return NULL;}
    cJSON_AddNumberToObject(root,"run",copy->run);
    cJSON_AddNumberToObject(root,"total",copy->total);
    cJSON_AddStringToObject(root,"filename_encoding","gbk");
    unsigned received=0;
    cJSON *array=cJSON_AddArrayToObject(root,"records");
    if(!array) {cJSON_Delete(root);free(copy);return NULL;}
    for(unsigned i=0;i<copy->total;++i) {
        if(!(copy->mask & (UINT64_C(1)<<i))) continue;
        ++received;SelfTestRecord *r=&copy->records[i];
        cJSON *item=cJSON_CreateObject();
        if(!item) {cJSON_Delete(root);free(copy);return NULL;}
        cJSON_AddItemToArray(array,item);
        cJSON_AddNumberToObject(item,"index",i);cJSON_AddNumberToObject(item,"code",r->code);
        cJSON_AddNumberToObject(item,"result",r->result);cJSON_AddNumberToObject(item,"hal_error",r->hal);
        cJSON_AddNumberToObject(item,"size",r->size);cJSON_AddNumberToObject(item,"read",r->read);
        cJSON_AddNumberToObject(item,"elapsed_ms",r->elapsed);
        char hex[SELFTEST_NAME_SIZE*2];static const char digits[]="0123456789ABCDEF";
        size_t j=0;
        for(;r->name[j];++j) {uint8_t c=(uint8_t)r->name[j];hex[j*2]=digits[c>>4];hex[j*2+1]=digits[c&15];}
        hex[j*2]=0;cJSON_AddStringToObject(item,"filename_hex",hex);
    }
    cJSON_AddNumberToObject(root,"received",received);
    cJSON_AddBoolToObject(root,"complete",copy->total && received==copy->total);
    free(copy);return root;
}
