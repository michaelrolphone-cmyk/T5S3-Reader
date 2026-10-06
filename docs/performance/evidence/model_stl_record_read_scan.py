#!/usr/bin/env python3
"""Count real model-loader reads through T5 SD/FatFs/HAL using a modeled card."""
from pathlib import Path
import subprocess,sys,os,argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source",type=Path)
parser.add_argument("--sanitize",action="store_true")
parser.add_argument("--app",action="store_true")
args=parser.parse_args()
S=(args.source or Path(__file__).resolve().parents[3]).resolve()
B=Path(__file__).resolve().parent/'model-stl-read-build';B.mkdir(exist_ok=True)
def extract(text,sig):
 a=text.index(sig);o=text.index('{',a);d=0
 for i in range(o,len(text)):
  d+=(text[i]=='{')-(text[i]=='}')
  if d==0:return text[a:i+1]
 raise Exception(sig)
(B/'app.c').write_text(r'''
#include <assert.h>
#include "model_viewer.c"
extern t5_storage_stream_t real_open(const char*,size_t*);
extern size_t real_read(t5_storage_stream_t,void*,size_t);
extern bool real_seek(t5_storage_stream_t,size_t);
extern void real_close(t5_storage_stream_t);
extern unsigned long long real_clock(void);
unsigned parse_polls;

static bool fake_poll(t5_app_input_t *out,uint32_t wait){assert(wait==1);memset(out,0,sizeof(*out));++parse_polls;return true;}
static const t5_app_api_v1 app_fixture={.abi_version=T5_APP_ABI_VERSION,.struct_size=sizeof(t5_app_api_v1),.poll=fake_poll};
static const t5_storage_api_v1 storage_fixture={.api_version=T5_STORAGE_API_VERSION,.struct_size=sizeof(t5_storage_api_v1),.stream_open=real_open,.stream_read=real_read,.stream_seek=real_seek,.stream_close=real_close};
void run_model(unsigned n){
 g_app=&app_fixture;g_storage=&storage_fixture;g_use_psram=false;parse_polls=0;
 mv_model_t model={0};assert(mv_load_model("/sd/probe.stl",&model));assert(model.triangle_count==n);
 assert(model.min.x==0&&model.min.y==0&&model.min.z==0);assert(model.max.x>0&&model.normalize>0);
 for(unsigned i=0;i<n;++i){mv_triangle_t*t=&model.triangles[i];assert(t->a.x==(float)(i%100));assert(t->a.y==(float)((i/100)%100));assert(t->a.z==0);assert(t->b.x==t->a.x+1&&t->b.y==t->a.y&&t->b.z==0);assert(t->c.x==t->a.x&&t->c.y==t->a.y+1&&t->c.z==1);}
 mv_model_free(&model);assert(!model.triangles&&!model.triangle_count);
}
''')
with (B/'app.c').open('a') as app_source:app_source.write((Path(__file__).resolve().parent/'model_stl_app_fixture.c').read_text())
base=(S/'test/storage_volume/runtime_test.cpp').read_text().split('#include "directory_iteration_test.inc"')[0]
base+='\n#include <T5StorageApi.h>\n#include <string>\n#include <map>\n'
t=(S/'src/native/NativePlatformBridge.cpp').read_text()
base+='HalFile streamFile;constexpr t5_storage_stream_t kStreamHandle=1u;\n'
for sig in ['bool mapStoragePath(','t5_storage_stream_t streamOpen(','size_t streamRead(','bool streamSeek(','void streamClose(']:base+=extract(t,sig)+'\n'
base+=r'''
unsigned hal_waits;
static unsigned reads,opens,seeks,closes;
static size_t bytes;
static std::map<size_t,unsigned> sizes;
extern "C" t5_storage_stream_t real_open(const char*p,size_t*s){++opens;return streamOpen(p,s);}
extern "C" size_t real_read(t5_storage_stream_t h,void*p,size_t n){++reads;++sizes[n];size_t got=streamRead(h,p,n);bytes+=got;return got;}
extern "C" bool real_seek(t5_storage_stream_t h,size_t n){++seeks;return streamSeek(h,n);}
extern "C" void real_close(t5_storage_stream_t h){++closes;streamClose(h);}
extern "C" void run_model(unsigned);
extern "C" void run_complete_app(unsigned);
extern "C" unsigned long long real_clock(void){return card_time;}
extern "C" unsigned parse_polls;
int main(int argc,char**argv){
 assert(argc==2);unsigned n=std::strtoul(argv[1],nullptr,10);assert(n&&n<=80000);
 card_image=static_cast<uint8_t*>(std::calloc(card_sectors,512));assert(card_image);format(false);
 const auto*driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);const auto*api=static_cast<const risc_storage_volume_api_v1*>(driver->capability);
 risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,now,sleep};risc_provider_dependency_v1 deps[]={{"platform.clock",1,&clock},{"spi.bus",1,&SpiCardFixture::api}};
 assert(driver->start(deps,2)&&Storage.bindVolume(api));
 std::vector<uint8_t> file(84u+50u*n);std::memcpy(file.data(),"Normal binary STL mesh",22);put32(file.data()+80,n);
 for(unsigned i=0;i<n;++i){uint8_t*p=file.data()+84+50*i;float x=float(i%100),y=float((i/100)%100);float vertices[9]={x,y,0,x+1,y,0,x,y+1,1};std::memcpy(p+12,vertices,sizeof(vertices));}
 {auto f=Storage.open("/probe.stl",O_WRONLY|O_CREAT|O_TRUNC);assert(f&&f.write(file.data(),file.size())==file.size()&&f.close());}
 unsigned sectors=card_reads,provider_sleeps=sleeps,locks=FakeLock::acquisitions;hal_waits=0;
#ifdef FULL_APP
 run_complete_app(n);
#else
 run_model(n);
#endif
 assert(reads==n+2&&sizes[50]==n&&sizes[84]==2&&hal_waits==n+2);assert(bytes==file.size()+84);assert(parse_polls==(n+511)/512);
 assert(opens==1&&seeks==2&&closes==1&&!streamFile.isOpen()&&!FakeLock::held&&Storage.generation().quiescent);
 printf("triangles=%u size=%zu stream_reads=%u row50=%u header84=%u bytes=%zu hal_waits=%u parse_polls=%u card_sector_reads=%u provider_waits=%u model_verified=1 closes=%u storage_locks=%u\n",n,file.size(),reads,sizes[50],sizes[84],bytes,hal_waits,parse_polls,card_reads-sectors,sleeps-provider_sleeps,closes,FakeLock::acquisitions-locks);
 assert(driver->quiesce());driver->stop();std::free(card_image);
}
'''
(B/'repro.cpp').write_text(base)
a=(S/'test/storage_volume/stubs/Arduino.h').read_text().replace('inline void delay(unsigned long n) { card_time += n; }','extern unsigned hal_waits;\ninline void delay(unsigned long n) { ++hal_waits; card_time += n; }')
(B/'Arduino.h').write_text(a)
san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
objects=[]
for path in ['Drivers/t5s3_sd/driver.c','Drivers/storage_fatfs/fatfs/ff.c','Drivers/storage_fatfs/fatfs/ffunicode.c','test/storage_volume/os_cpu_fake.c',str(B/'app.c')]:
 obj=B/(Path(path).name+'.o');subprocess.run(['cc','-std=c11','-O1','-g',*san,'-ffunction-sections','-fdata-sections','-pthread','-D_XOPEN_SOURCE=700','-Isdk/driver','-I'+str(B),'-IApps','-Ilib/NativeApps/include','-c',path,'-o',str(obj)],cwd=S,check=True);objects.append(str(obj))
subprocess.run(['c++','-std=c++17','-O1','-g',*san,'-Wl,--gc-sections','-pthread','-DBOARD_T5S3_PRO','-DTEST_SPI_TRANSPORT',*(['-DFULL_APP'] if '--app' in sys.argv else []),'-I'+str(B),'-Itest/storage_volume','-Itest/storage_volume/stubs','-Ilib/hal','-Isdk/driver','-Ilib/NativeApps/include',str(B/'repro.cpp'),'lib/hal/HalStorageVolume.cpp',*objects,'-o',str(B/'repro')],cwd=S,check=True)
for n in [1,512,1000,10000,80000]:subprocess.run([str(B/'repro'),str(n)],cwd=S,check=True,timeout=180,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
