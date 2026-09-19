"""从项目随附ChaN GBK表生成等价的紧凑表，保留原作者声明和大小写转换。"""
from pathlib import Path
import re

directory = Path(__file__).resolve().parent
source = (directory.parent / 'Middlewares/Third_Party/FatFs/src/option/cc936.c').read_text()
def table(name):
    """从原始C数组name读取码点对。"""
    body = re.search(r'const WCHAR '+name+r'\[\] = \{(.*?)\};', source, re.S).group(1)
    nums = [int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]+', body)]
    return dict(zip(nums[::2],nums[1::2]))

original = table('oem2uni')
dense = [0] * (126*190)
for code, value in original.items():
    if code in (0,0x80): continue
    high, low = code >> 8, code & 255
    assert 0x81 <= high <= 0xFE and 0x40 <= low <= 0xFE and low != 0x7F
    dense[(high-0x81)*190 + low-0x40-(low>0x7F)] = value
for code in range(128,65536):
    high, low = code >> 8, code & 255
    got = dense[(high-0x81)*190+low-0x40-(low>0x7F)] if 0x81<=high<=0xFE and 0x40<=low<=0xFE and low!=0x7F else 0
    if code==0x80: got=0x20AC
    assert got == original.get(code,0), hex(code)

prefix = source[:source.index('static\nconst WCHAR oem2uni')]
prefix = prefix.replace('#include "../ff.h"','#include "ff.h"')
array = '/* GBK双字节直接索引表，查找复杂度O(1)，无效码点为0。 */\nstatic const WCHAR gbk_to_unicode[] = {\n'
for i in range(0,len(dense),12):
    array += '    '+','.join(f'0x{x:04X}' for x in dense[i:i+12])+',\n'
array += '};\n'
convert = '''
/**
  * @brief 在GBK与Unicode之间转换，chr为码点，dir为0时转GBK、非0时转Unicode。
  * @retval 转换结果，0表示无法转换。
  */
WCHAR ff_convert(WCHAR chr, UINT dir)
{
    if(chr<0x80) return chr;
    if(dir) {
        if(chr==0x80) return 0x20AC;
        unsigned high=chr>>8,low=chr&255;
        if(high<0x81 || high>0xFE || low<0x40 || low>0xFE || low==0x7F) return 0;
        return gbk_to_unicode[(high-0x81)*190+low-0x40-(low>0x7F)];
    }
    unsigned lo=0,hi=sizeof(uni2oem)/sizeof(uni2oem[0])/2;
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        if(uni2oem[mid*2]==chr) return uni2oem[mid*2+1];
        if(uni2oem[mid*2]<chr) lo=mid+1; else hi=mid;
    }
    return 0;
}
/** @brief 将Unicode码点chr转换为大写，保持ChaN原始转换规则。 @retval 大写码点。 */
'''
upper=source[source.index('WCHAR ff_wtoupper'):]
(directory/'ff_gbk.c').write_text(prefix+array+convert+upper,encoding='utf-8')
print('GBK mapping verified; reverse table bytes:',len(original)*4,'->',len(dense)*2)
