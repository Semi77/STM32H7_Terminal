"""编译原表和紧凑表，穷举比较全部16位码点的双向转换及大小写结果。"""
from pathlib import Path
import subprocess
import tempfile

directory=Path(__file__).resolve().parent
source=directory.parent/'Middlewares/Third_Party/FatFs/src/option/cc936.c'
stub='typedef unsigned short WCHAR; typedef unsigned UINT;\n#define _USE_LFN 2\n#define _CODE_PAGE 936\n'
with tempfile.TemporaryDirectory(prefix='gbk_check_') as temp:
    work=Path(temp)
    original=source.read_text().replace('#include "../ff.h"',stub)
    original=original.replace('ff_convert','original_convert').replace('ff_wtoupper','original_upper')
    compact=(directory/'ff_gbk.c').read_text(encoding='utf-8').replace('#include "ff.h"',stub)
    (work/'original.c').write_text(original,encoding='utf-8')
    (work/'compact.c').write_text(compact,encoding='utf-8')
    (work/'test.c').write_text('''
#include <assert.h>
#include <stdio.h>
typedef unsigned short WCHAR;
WCHAR ff_convert(WCHAR,unsigned); WCHAR original_convert(WCHAR,unsigned);
WCHAR ff_wtoupper(WCHAR); WCHAR original_upper(WCHAR);
int main(void) {
  for(unsigned c=0;c<65536;++c) {
    assert(ff_convert(c,0)==original_convert(c,0));
    assert(ff_convert(c,1)==original_convert(c,1));
    assert(ff_wtoupper(c)==original_upper(c));
  }
  puts("196608 GBK/Unicode/case comparisons passed");return 0;
}
''')
    exe=work/'test.exe'
    subprocess.run(['gcc','-std=c11','-O2',str(work/'original.c'),str(work/'compact.c'),str(work/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
