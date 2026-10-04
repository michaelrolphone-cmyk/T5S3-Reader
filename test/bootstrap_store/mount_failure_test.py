from pathlib import Path
import os, re, subprocess

ROOT=Path(__file__).resolve().parents[2]
import tempfile
_temp=tempfile.TemporaryDirectory()
OUT=Path(_temp.name)
raw=(ROOT/'src/platform/SdBootReader.cpp').read_text()
source=re.sub(r'^#include .*\n', '', raw, flags=re.M)
prefix=r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define BOARD_XTEINK_X4_PRO 1
#include "x4pro_pins.h"
using esp_err_t=int; using gpio_num_t=int; using TaskHandle_t=void*;
using BYTE=uint8_t; using WORD=uint16_t; using DWORD=uint32_t; using UINT=unsigned; using LBA_t=uint32_t;
using DSTATUS=int; using DRESULT=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_TIMEOUT=0x107,ESP_ERR_INVALID_SIZE=0x104,ESP_ERR_INVALID_STATE=0x103,ESP_ERR_NOT_SUPPORTED=0x106;
constexpr int GPIO_MODE_OUTPUT=2,GPIO_MODE_INPUT=1,GPIO_FLOATING=0;
constexpr int STA_NOINIT=1,STA_PROTECT=4,RES_PARERR=4,RES_WRPRT=2,RES_ERROR=1,RES_OK=0,RES_NOTRDY=3;
constexpr int CTRL_SYNC=0,GET_SECTOR_COUNT=1,GET_SECTOR_SIZE=2,GET_BLOCK_SIZE=3,FR_OK=0,FA_READ=1;
constexpr int SDMMC_HOST_FLAG_1BIT=1,SDMMC_FREQ_DEFAULT=20000;
struct FATFS{}; struct FIL{unsigned size=16;};
unsigned f_size(FIL* f){return f->size;}
struct sdmmc_command_t{int timeout_ms;};
struct sdmmc_host_t{int flags=0,slot=1,max_freq_khz=20000,command_timeout_ms=0;esp_err_t(*do_transaction)(int,sdmmc_command_t*)=nullptr;};
struct sdmmc_slot_config_t{int width=0,clk=-1,cmd=-1,d0=-1;unsigned flags=0;};
struct sdmmc_card_t{struct{int sector_size=512;uint64_t capacity=1024;}csd;};
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{}
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
static std::string fault,events;
static int64_t clockUs=0;
static unsigned nativeCommands=0,seenTimeout=0,initCalls=0;
static bool ownTask=true,controller=false;
TaskHandle_t xTaskGetCurrentTaskHandle(){return ownTask?reinterpret_cast<void*>(1):reinterpret_cast<void*>(2);}
int64_t esp_timer_get_time(){return clockUs;}
void vTaskDelay(unsigned ticks){clockUs+=ticks*1000;}
unsigned pdMS_TO_TICKS(unsigned ms){return ms;}
int gpio_hold_dis(int pin){assert(pin==5);events+='h';return fault=="hold"?ESP_FAIL:ESP_OK;}
int gpio_set_direction(int,int){return ESP_OK;}
int gpio_set_level(int pin,unsigned level){if(pin==5)events+=level?'O':'N';return ESP_OK;}
int gpio_set_pull_mode(int,int){return ESP_OK;}
int gpio_hold_en(int pin){assert(pin==5);events+='H';return fault=="park"?ESP_FAIL:ESP_OK;}
int sdmmc_host_init(){++initCalls;events+='I';controller=fault!="host";return controller?ESP_OK:ESP_FAIL;}
int sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t* c){assert(slot==1&&c->width==1&&c->clk==41&&c->cmd==42&&c->d0==40);events+='S';return fault=="slot"?ESP_FAIL:ESP_OK;}
int sdmmc_host_do_transaction(int,sdmmc_command_t* c){++nativeCommands;seenTimeout=c->timeout_ms;if(fault=="command-timeout"){clockUs+=c->timeout_ms*1000;return ESP_ERR_TIMEOUT;}return ESP_OK;}
int sdmmc_card_init(const sdmmc_host_t* host,sdmmc_card_t* card){events+='C';assert(host->flags==1&&host->command_timeout_ms==250);sdmmc_command_t cmd{9999};int result=host->do_transaction(host->slot,&cmd);if(fault=="budget"){clockUs=20000000;cmd.timeout_ms=9999;result=host->do_transaction(host->slot,&cmd);}card->csd={512,1024};return fault=="card"?ESP_FAIL:result;}
int sdmmc_host_deinit(){events+='D';if(fault=="deinit")return ESP_FAIL;controller=false;return ESP_OK;}
int sdmmc_read_sectors(sdmmc_card_t*,void* dst,unsigned,unsigned){memset(dst,0,512);return ESP_OK;}
int f_mount(FATFS* fs,const char*,int){events+=fs?'F':'U';return fs&&fault=="fat"?ESP_FAIL:ESP_OK;}
int f_open(FIL*,const char*,int){events+='r';return fault=="open"?ESP_FAIL:ESP_OK;}
int f_read(FIL*,void* dst,unsigned bytes,unsigned* got){memset(dst,0,bytes);*got=bytes;return ESP_OK;}
int f_close(FIL*){events+='c';return fault=="close"?ESP_FAIL:ESP_OK;}
'''
suffix=r'''
int main(int argc,char**argv){
  assert(argc==2);fault=argv[1];
  bool ok=SdBootReader::mount();
  if(fault=="okay"||fault=="close"||fault=="open"||fault=="deinit"||fault=="park"){
    assert(ok&&SdBootReader::retained()&&controller);
    assert(events.substr(0,7)=="hONISCF"); // Native controller starts after the completed cycle.
    std::vector<uint8_t> bytes;
    bool read=SdBootReader::read("/sdboot/System/Config/boot.json",4096,bytes);
    assert(read==(fault!="close"&&fault!="open"));
    if(fault=="close"){auto before=events;assert(!SdBootReader::read("/sdboot/x",32,bytes)&&events==before);}
    bool released=SdBootReader::release();
    assert(released==(fault!="close"&&fault!="deinit"&&fault!="park"));
    assert(SdBootReader::retained()==!released);
    if(!released){auto before=events;assert(!SdBootReader::mount()&&events==before);}
  } else {
    assert(!ok&&!controller);
    assert(SdBootReader::retained()==(fault=="hold"));
    if(fault=="command-timeout")assert(nativeCommands==1&&seenTimeout==250&&clockUs==451000);
    if(fault=="budget")assert(nativeCommands==1&&clockUs==20000000);
  }
  printf("actual SdBootReader case=%s ok=%d elapsed_ms=%lld retained=%d calls=%u events=%s\n",fault.c_str(),ok,(long long)(clockUs/1000),SdBootReader::retained(),nativeCommands,events.c_str());
}
'''
cpp=OUT/'mount_probe.cpp';cpp.write_text(prefix+source+suffix)
binary=OUT/'mount_probe'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'Drivers/x4pro_board'),str(cpp),'-o',str(binary)],check=True)
for case in ['okay','hold','host','slot','card','command-timeout','budget','fat','open','close','deinit','park']:
    subprocess.run([str(binary),case],check=True,env=os.environ,timeout=10)
