from pathlib import Path
import subprocess,sys,os
S=Path(__file__).resolve().parents[3];B=Path(__file__).resolve().parent/'browser-resume-build';B.mkdir(exist_ok=True)
sys.path.insert(0,str(S/'test/storage_volume'))
from mmio_boundary import header

def extract(text,sig):
 a=text.index(sig);o=text.index('{',a);d=0
 for i in range(o,len(text)):
  d+=(text[i]=='{')-(text[i]=='}')
  if d==0:return a,i+1,text[a:i+1]
 raise Exception(sig)
def f(path,sig):return extract((S/path).read_text(),sig)[2]
t=(S/'test/native_apps/file_browser_test.c').read_text();t=t[:t.index('int main(')]
t=t.replace('#include "../../Apps/file_browser.c"','#include "file_browser.c"\nextern bool real_dir_open(const char*);\nextern bool real_dir_next(t5_app_dirent_t*);\nextern void real_dir_close(void);\nextern void first_frame(const char*,uint32_t,int32_t);\nstatic bool nested_case, result_available;')
replacements={
'static bool dir_open(':'static bool dir_open(const char*p){return real_dir_open(p);}',
'static bool dir_next(':'static bool dir_next(t5_app_dirent_t*p){return real_dir_next(p);}',
'static void dir_close(':'static void dir_close(void){real_dir_close();}',
'static void render_browser(':'''static void render_browser(const char*p,const char*s,const t5_file_browser_entry_t*e,uint32_t n,int32_t selected){(void)s;assert(n);if(phase==2){assert(render_index==0);assert(!strcmp(p,nested_case?"/Books":"/"));assert(selected>=0 && !strcmp(e[selected].name,"image000.bmp"));first_frame(p,n,selected);}++render_index;}''',
'static bool poll_browser(':'''static bool poll_browser(t5_file_browser_event_t*e,uint32_t wait,bool root,bool directory){(void)root;(void)directory;assert(wait==20);memset(e,0,sizeof(*e));if(phase==2){e->type=T5_FILE_BROWSER_EVENT_EXIT;event_index++;return true;}assert(phase==1);if(nested_case && event_index<2){e->type=event_index==0?T5_FILE_BROWSER_EVENT_ROW:T5_FILE_BROWSER_EVENT_OPEN;e->row_index=0;}else{e->type=(event_index-(nested_case?2:0))==0?T5_FILE_BROWSER_EVENT_ROW:T5_FILE_BROWSER_EVENT_OPEN;e->row_index=nested_case?0:1;}event_index++;assert(event_index<=4);return true;}''',
'static uint32_t handler_count(':'static uint32_t handler_count(const char*p){assert(strstr(p,"image000.bmp"));return 1;}',
'static bool handler_get(':'''static bool handler_get(const char*p,uint32_t i,t5_file_handler_t*out){assert(p&&i==0);memset(out,0,sizeof(*out));out->kind=T5_FILE_HANDLER_APP;strcpy(out->app_id,"image_viewer");strcpy(out->display_name,"Image Viewer");return true;}''',
'static bool open_request(':'''static bool open_request(const char*p,const char*a,uint64_t c){assert(strstr(p,"image000.bmp")&&!strcmp(a,"image_viewer")&&c==HANDOFF_COOKIE);assert(session_exists);open_requested=true;return true;}''',
'static bool open_take_result(':'''static bool open_take_result(int32_t*error,uint64_t*cookie){if(phase!=2||!result_available)return false;result_available=false;*error=0;*cookie=HANDOFF_COOKIE;return true;}''',
'static bool confirm_take(':'static bool confirm_take(bool*c,uint64_t*k){(void)c;(void)k;return false;}'
}
for sig,new in replacements.items():a,b,_=extract(t,sig);t=t[:a]+new+t[b:]
t+='''
void begin_browser_handoff(bool nested){nested_case=nested;phase=1;event_index=render_index=0;session_exists=false;open_requested=false;back_exit_false_count=back_exit_true_count=0;app_main();assert(open_requested&&session_exists);assert(back_exit_false_count==1&&back_exit_true_count==1);}
void resume_browser(void){phase=2;event_index=render_index=0;result_available=true;app_main();assert(event_index==1&&render_index==1&&!session_exists&&!result_available);assert(back_exit_false_count==2&&back_exit_true_count==2);}
'''
(B/'app.c').write_text(t)
# Keep complete real wire/card and production HAL. Only user interfaces and handler execution are fixtures.
base=(S/'test/storage_volume/runtime_test.cpp').read_text();base=base[:base.index('#include "directory_iteration_test.inc"')]
base+='''
#include <T5AppApi.h>
#include "native/BmpLayout.h"
#include <map>
#include <string>
struct Session{HalFile directory;} state;
Session* current(){return &state;}
'''
for sig in ['const char* storagePath(','bool dirOpen(','bool dirNext(','void dirClose(']:base+=f('src/native/NativeAppHost.cpp',sig)+'\n'
base+='''
struct Record{std::string path;unsigned sectors;unsigned nexts;};
std::vector<Record> records;unsigned start_reads,returned_entries,next_calls,open_calls,close_calls,frame_calls;
extern "C" bool real_dir_open(const char*p){unsigned before=card_reads;bool ok=dirOpen(p);assert(ok);records.push_back({p,card_reads-before,0});start_reads=card_reads;++open_calls;return ok;}
extern "C" bool real_dir_next(t5_app_dirent_t*p){++next_calls;++records.back().nexts;bool ok=dirNext(p);if(ok)++returned_entries;return ok;}
extern "C" void real_dir_close(){dirClose();records.back().sectors+=card_reads-start_reads;++close_calls;}
extern "C" void first_frame(const char*p,uint32_t n,int32_t selected){assert(!state.directory.isOpen());++frame_calls;printf("FRAME path=%s count=%u selected=%d opens=%u closes=%u next_calls=%u returned=%u",p,n,selected,open_calls,close_calls,next_calls,returned_entries);unsigned total=0;for(auto &r:records){total+=r.sectors;printf(" scan[%s]=%u/%u",r.path.c_str(),r.sectors,r.nexts);}printf(" sectors=%u\\n",total);}
static void install_bmp(const char*p){uint8_t b[58]{};b[0]='B';b[1]='M';put32(b+2,sizeof(b));put32(b+10,54);put32(b+14,40);put32(b+18,1);put32(b+22,1);put16(b+26,1);put16(b+28,24);put32(b+34,4);NativeImage::BmpLayout layout{};assert(NativeImage::readBmpLayout(b,sizeof(b),layout));auto file=Storage.open(p,O_WRONLY|O_CREAT|O_TRUNC);assert(file&&file.write(b,sizeof(b))==sizeof(b)&&file.close());}
extern "C" void begin_browser_handoff(bool);
extern "C" void resume_browser(void);
int main(int argc,char**argv){assert(argc==3);unsigned count=std::strtoul(argv[1],nullptr,10);bool nested=std::atoi(argv[2]);assert(count>=8&&count<=200);card_image=static_cast<uint8_t*>(std::calloc(card_sectors,512));assert(card_image);format(false);const auto*driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);const auto*api=static_cast<const risc_storage_volume_api_v1*>(driver->capability);risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,now,sleep};risc_provider_dependency_v1 deps[]={{"platform.clock",1,&clock}};assert(driver->start(deps,1)&&Storage.bindVolume(api));assert(Storage.mkdir("/Books"));for(unsigned i=0;i<count;++i){char name[80];snprintf(name,sizeof(name),"/image%03u.bmp",i);install_bmp(name);snprintf(name,sizeof(name),"/Books/image%03u.bmp",i);install_bmp(name);}begin_browser_handoff(nested);records.clear();open_calls=close_calls=next_calls=returned_entries=frame_calls=0;resume_browser();assert(frame_calls==1&&open_calls==3&&close_calls==3);assert(records.size()==3);assert(records[0].path=="/sd");assert(records[1].path==(nested?"/sd/Books":"/sd"));assert(records[2].path==records[1].path);assert(records[1].nexts==records[2].nexts);assert(returned_entries==(nested?3*count+1:3*(count+1)));assert(!state.directory.isOpen()&&!FakeLock::held&&Storage.generation().quiescent);assert(driver->quiesce());driver->stop();std::free(card_image);}
'''
(B/'repro.cpp').write_text(base);(B/'x4pro_mmio.h').write_text(header(S))
san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
objects=[]
for path in ['Drivers/x4pro_sd/driver.c','Drivers/storage_fatfs/fatfs/ff.c','Drivers/storage_fatfs/fatfs/ffunicode.c','test/storage_volume/os_cpu_fake.c',str(B/'app.c')]:
 obj=B/(Path(path).name+'.o');subprocess.run(['cc','-std=c11','-O1','-g',*san,'-pthread','-D_XOPEN_SOURCE=700','-Isdk/driver','-I'+str(B),'-Itest/storage_volume/fake','-IDrivers/x4pro_board','-IApps','-Ilib/NativeApps/include','-c',path,'-o',str(obj)],cwd=S,check=True);objects.append(str(obj))
subprocess.run(['c++','-std=c++17','-O1','-g',*san,'-pthread','-DBOARD_XTEINK_X4_PRO','-Itest/storage_volume','-Itest/storage_volume/stubs','-Ilib/hal','-Isdk/driver','-Isrc','-Ilib/NativeApps/include','-I'+str(B),str(B/'repro.cpp'),'lib/hal/HalStorageVolume.cpp',*objects,'-o',str(B/'repro')],cwd=S,check=True)
for n in [8,32,64,128,200]:
 for nested in [0,1]:subprocess.run([str(B/'repro'),str(n),str(nested)],cwd=S,check=True,timeout=120,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
