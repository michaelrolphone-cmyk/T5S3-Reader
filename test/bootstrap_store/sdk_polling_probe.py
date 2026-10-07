"""Execute pinned SDK reset polling; external timeout bounds the negative probe."""
from pathlib import Path
import re,subprocess,sys,hashlib,tempfile
assert len(sys.argv)==2, 'Pass the pinned IDF4.4.7 components/driver/sdmmc_host.c path'
source=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(source).hexdigest()=='f78e339f28cb430f658ae5344d024ab9412b69bca7988be17e8cf3b35fc8856a', 'SDK source differs from the audited pin'
_temp=tempfile.TemporaryDirectory()
OUT=Path(_temp.name)
sdk=source.decode()
body=sdk.split('void sdmmc_host_reset(void)\n{',1)[1].split('\n}\n',1)[0]
code=r'''
#include <cstdio>
#include <cstring>
#include <cstdint>
static bool stuck=false,reported=false;
static uint64_t elapsedUs=0;
struct RegisterBit {
 unsigned value=0,reads=0;
 RegisterBit& operator=(unsigned v){value=v;reads=0;return *this;}
 operator bool(){
  elapsedUs+=1000;
  if(!stuck&&++reads>3)value=0;
  if(elapsedUs>=20000000&&!reported){reported=true;puts("wrapper 20 s deadline passed while SDK reset still owns execution");fflush(stdout);}
  return value!=0;
 }
};
struct {struct {RegisterBit controller_reset,fifo_reset,dma_reset;}ctrl;}SDMMC;
void sdmmc_host_reset(void){
'''+body+r'''
}
int main(int argc,char**argv){stuck=argc>1&&!strcmp(argv[1],"stuck");sdmmc_host_reset();printf("SDK reset returned at modeled %llu us\n",(unsigned long long)elapsedUs);}
'''
cpp=OUT/'sdk_polling_probe.cpp';cpp.write_text(code)
binary=OUT/'sdk_polling_probe'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True)
import os
env=os.environ
subprocess.run([str(binary)],check=True,env=env,timeout=2)
try:
 subprocess.run([str(binary),'stuck'],check=True,env=env,timeout=0.25,capture_output=True,text=True)
 raise AssertionError('stuck register unexpectedly returned')
except subprocess.TimeoutExpired as exc:
 out=exc.stdout.decode() if isinstance(exc.stdout,bytes) else exc.stdout
 assert 'wrapper 20 s deadline passed' in out
 print(out.strip())
 print('Expected negative: parent killed stuck SDK hardware poll after 250 ms host time; Reader wrapper cannot regain control.')
