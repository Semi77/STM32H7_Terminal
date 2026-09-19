#include "selftest_wire.h"
#include <assert.h>
/** @brief 验证协议往返、损坏拒绝、空文件名及最长文件名。 @retval 0表示通过。 */
int main(void)
{
    SelfTestRecord r={.run=123,.index=0,.total=2,.code=2,.size=4096,.read=4096}, out;
    char line[SELFTEST_LINE_SIZE];uint32_t crc,decoded;
    memset(r.name,'A',sizeof(r.name)-1);
    size_t n=SelfTest_Encode(&r,line,&crc);assert(n>2);line[n-2]=0;
    assert(SelfTest_Decode(line,&out,&decoded));assert(crc==decoded);
    assert(out.read==4096 && strcmp(out.name,r.name)==0);
    line[10]^=1;assert(!SelfTest_Decode(line,&out,&decoded));
    r.name[0]=0;r.index=1;r.code=10;
    n=SelfTest_Encode(&r,line,&crc);line[n-2]=0;
    assert(SelfTest_Decode(line,&out,&decoded));assert(out.name[0]==0);
    r.index=2;n=SelfTest_Encode(&r,line,&crc);line[n-2]=0;
    assert(!SelfTest_Decode(line,&out,&decoded));
    puts("selftest wire tests passed");return 0;
}
