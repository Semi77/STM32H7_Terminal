#ifndef SELFTEST_WIRE_H
#define SELFTEST_WIRE_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SELFTEST_MAX_RECORDS 36U
#define SELFTEST_NAME_SIZE 256U
#define SELFTEST_LINE_SIZE 768U
typedef struct {
    uint32_t run, index, total, code, result, hal, size, read, elapsed;
    char name[SELFTEST_NAME_SIZE];
} SelfTestRecord;

/** @brief 计算串口报告校验值；data为字节串，size为长度。 @retval CRC32。 */
static inline uint32_t SelfTest_Crc(const char *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    while (size--) {
        crc ^= (uint8_t)*data++;
        for (unsigned i=0; i<8; ++i) crc=(crc>>1)^((0U-(crc&1U))&0xEDB88320U);
    }
    return crc ^ UINT32_MAX;
}

/** @brief 把十六进制字符c转换为数值。 @retval 0至15，非法字符返回-1。 */
static inline int SelfTest_Hex(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}

/** @brief 将记录r编码到line，文件名按GBK字节转十六进制；crc接收确认标识。 @retval 行长度，失败返回0。 */
static inline size_t SelfTest_Encode(const SelfTestRecord *r, char line[SELFTEST_LINE_SIZE], uint32_t *crc)
{
    int n=snprintf(line, SELFTEST_LINE_SIZE,
        "SELFTEST:%lu:%lu:%lu:%lu:%lu:%lu:%lu:%lu:%lu:",
        (unsigned long)r->run,(unsigned long)r->index,(unsigned long)r->total,
        (unsigned long)r->code,(unsigned long)r->result,(unsigned long)r->hal,
        (unsigned long)r->size,(unsigned long)r->read,(unsigned long)r->elapsed);
    if(n<0 || n>= (int)SELFTEST_LINE_SIZE-12) return 0;
    static const char digits[]="0123456789ABCDEF";
    size_t i=0;
    for(; i<SELFTEST_NAME_SIZE && r->name[i]; ++i) {
        if(n+14 >= (int)SELFTEST_LINE_SIZE) return 0;
        uint8_t c=(uint8_t)r->name[i];
        line[n++]=digits[c>>4]; line[n++]=digits[c&15];
    }
    if(i==SELFTEST_NAME_SIZE) return 0;
    *crc=SelfTest_Crc(line,(size_t)n);
    return (size_t)n+(size_t)snprintf(line+n,SELFTEST_LINE_SIZE-(size_t)n,":%08lX\r\n",(unsigned long)*crc);
}

/** @brief 校验并解析不含换行的报告line，输出记录r及确认值crc。 @retval true表示完整合法。 */
static inline bool SelfTest_Decode(const char *line, SelfTestRecord *r, uint32_t *crc)
{
    if(strncmp(line,"SELFTEST:",9)!=0 || strlen(line)>=SELFTEST_LINE_SIZE) return false;
    const char *last=strrchr(line,':');
    if(!last || strlen(last+1)!=8) return false;
    uint32_t check=0;
    for(unsigned i=1;i<=8;++i) { int h=SelfTest_Hex(last[i]); if(h<0)return false; check=(check<<4)|(uint32_t)h; }
    if(check!=SelfTest_Crc(line,(size_t)(last-line))) return false;
    uint32_t values[9]; const char *p=line+9;
    for(unsigned i=0;i<9;++i) {
        uint32_t value=0; unsigned digits=0;
        while(p<last && *p>='0' && *p<='9') {
            unsigned d=(unsigned)(*p++-'0');
            if(value>(UINT32_MAX-d)/10U) return false;
            value=value*10U+d; ++digits;
        }
        if(!digits || p>=last || *p++!=':') return false;
        values[i]=value;
    }
    size_t length=(size_t)(last-p);
    if((length&1U) || length/2>=SELFTEST_NAME_SIZE || !values[2] || values[2]>SELFTEST_MAX_RECORDS || values[1]>=values[2] || values[3]>13) return false;
    memset(r,0,sizeof(*r));
    r->run=values[0];r->index=values[1];r->total=values[2];r->code=values[3];
    r->result=values[4];r->hal=values[5];r->size=values[6];r->read=values[7];r->elapsed=values[8];
    for(size_t i=0;i<length/2;++i) {
        int a=SelfTest_Hex(p[i*2]),b=SelfTest_Hex(p[i*2+1]);
        if(a<0 || b<0 || (a==0 && b==0)) return false;
        r->name[i]=(char)((a<<4)|b);
    }
    *crc=check;return true;
}
#endif
