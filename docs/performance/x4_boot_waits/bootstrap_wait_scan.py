from pathlib import Path
import ast,re,subprocess,sys,os,argparse,hashlib
parser=argparse.ArgumentParser(description="Count boot scheduler requests using the exact production Reader and IDF sources; host model, not hardware timing.")
parser.add_argument('source',type=Path);parser.add_argument('card_root',type=Path);parser.add_argument('arduino_json',type=Path);parser.add_argument('sdk_command_source',type=Path);parser.add_argument('output',type=Path);parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args();D=Path(__file__).resolve().parent;R=args.source.resolve();san=args.sanitize;B=args.output.resolve();B.mkdir(parents=True,exist_ok=True)
assert hashlib.sha256(args.sdk_command_source.read_bytes()).hexdigest()=='c1668f6932a12448bea2989e69497aca62cf3c4ad22bb3ef4ef8fa6efb5d3e3c'
writer_objects=[]
for name in ['ff','ffunicode']:
 obj=B/(name+'-writer.o');subprocess.run(['cc','-std=c11','-O1','-c',str(R/'Drivers/storage_fatfs/fatfs'/(name+'.c')),'-o',str(obj)],check=True);writer_objects.append(str(obj))
subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-I'+str(R/'Drivers/storage_fatfs/fatfs'),str(D/'bootstrap_card_writer.cpp'),*writer_objects,'-o',str(B/'card_writer')],check=True)
subprocess.run([str(B/'card_writer'),str(args.card_root.resolve()),str(B/'card.img')],check=True)

script=ast.parse((R/'test/bootstrap_store/mount_failure_test.py').read_text());prefix=next(ast.literal_eval(x.value) for x in script.body if isinstance(x,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='prefix' for t in x.targets))
prefix=prefix.replace('using BYTE=uint8_t; using WORD=uint16_t; using DWORD=uint32_t; using UINT=unsigned; using LBA_t=uint32_t;','')
prefix=prefix.replace('using DSTATUS=int; using DRESULT=int;','')
prefix=re.sub(r'constexpr int STA_NOINIT=.*?\n','',prefix)
prefix=re.sub(r'constexpr int CTRL_SYNC=.*?\n','',prefix)
prefix=prefix.replace('struct FATFS{}; struct FIL{unsigned size=16;};\nunsigned f_size(FIL* f){return f->size;}','')
prefix=prefix.replace('capacity=1024;}csd;','capacity=131072;}csd; sdmmc_host_t host;')
prefix=prefix.replace('static int64_t clockUs=0;','static int64_t clockUs=0; static unsigned tickUs=1000; static std::vector<uint8_t> media; static unsigned sectorCalls, readerCalls; static size_t readerBytes; static std::map<std::string,unsigned> waits;')
prefix=prefix.replace('void vTaskDelay(unsigned ticks){clockUs+=ticks*1000;}','void hostDelay(const char* function,unsigned ticks){waits[function]++;clockUs+=ticks*tickUs;}\n#define vTaskDelay(t) hostDelay(__func__,t)\nuint32_t millis(){return clockUs/1000;}\nvoid delay(unsigned ms){clockUs+=ms*1000;waits["ArduinoDelay"]++;}')
prefix=prefix.replace('card->csd={512,1024};','card->csd={512,131072};card->host=*host;')
prefix=re.sub(r'int sdmmc_read_sectors\([^\n]+', 'int sdmmc_read_sectors(sdmmc_card_t* card,void* dst,unsigned sector,unsigned count){assert(count==1);++sectorCalls;sdmmc_command_t cmd{250};const int result=card->host.do_transaction(card->host.slot,&cmd);if(result!=ESP_OK)return result;memcpy(dst,media.data()+size_t(sector)*512,512);return ESP_OK;}',prefix)

prefix=prefix.replace('struct sdmmc_command_t{int timeout_ms;};','struct sdmmc_command_t{int opcode=0;uint32_t arg=0;int flags=0;size_t blklen=0;void* data=nullptr;size_t datalen=0;int timeout_ms=0;uint32_t response[4]{};int error=0;};')
prefix=prefix.replace('sdmmc_host_t host;','sdmmc_host_t host;uint32_t ocr=1;uint32_t rca=1;')
prefix=prefix.replace('sdmmc_command_t cmd{9999}','sdmmc_command_t cmd{.timeout_ms=9999}')
prefix=re.sub(r'int sdmmc_read_sectors\([^\n]+','',prefix)
prefix=prefix.replace('return ESP_OK;}\nint sdmmc_card_init','if(c->opcode==17){++sectorCalls;memcpy(c->data,media.data()+size_t(c->arg)*512,c->datalen);}c->response[0]=0x100;return ESP_OK;}\nint sdmmc_card_init')
sdk=args.sdk_command_source.read_text()
def function(name):
 start=sdk.index('esp_err_t '+name+'(');end=sdk.index('\n}\n',start)+3;return sdk[start:end]
compat=r"""
#define SCF_CMD_AC 1
#define SCF_CMD_ADTC 2
#define SCF_CMD_READ 4
#define SCF_RSP_R1 8
#define MMC_SEND_STATUS 13
#define MMC_READ_BLOCK_SINGLE 17
#define MMC_READ_BLOCK_MULTIPLE 18
#define MMC_R1_READY_FOR_DATA 0x100
#define SD_OCR_SDHC_CAP 1
#define MMC_ARG_RCA(r) ((r)<<16)
#define MMC_R1(response) ((response)[0])
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define MALLOC_CAP_DMA 1
constexpr int ESP_ERR_NO_MEM=-2;
static bool host_is_spi(sdmmc_card_t*) {return false;}
static bool esp_ptr_dma_capable(void*) {return true;}
static void* heap_caps_malloc(size_t n,unsigned) {return malloc(n);}
static esp_err_t sdmmc_send_cmd(sdmmc_card_t* card,sdmmc_command_t* cmd){cmd->timeout_ms=250;return card->host.do_transaction(card->host.slot,cmd);}
"""
prefix+=compat+function('sdmmc_send_cmd_send_status')+function('sdmmc_read_sectors_dma')+function('sdmmc_read_sectors')
prefix=re.sub(r'^int f_(?:mount|open|read|close)\([^\n]+\n','',prefix,flags=re.M)
prefix=prefix.replace('#include <algorithm>','#include <algorithm>\n#include <map>\n#include <fstream>\n#include "platform/SdBootFatFs.h"\n#include "runtime/drivers/BootstrapModuleStore.h"')
source=re.sub(r'^#include .*\n','',(R/'src/platform/SdBootReader.cpp').read_text(),flags=re.M)
suffix=r'''
static bool countedRead(const std::string& path,size_t limit,std::vector<uint8_t>& bytes){++readerCalls;const auto before=sectorCalls;bool ok=SdBootReader::read(path,limit,bytes);readerBytes+=bytes.size();printf("READ %s success=%d bytes=%zu sectors=%u\n",path.c_str(),ok,bytes.size(),sectorCalls-before);return ok;}
int main(int argc,char**argv){assert(argc==3);tickUs=atoi(argv[2]);std::ifstream f(argv[1],std::ios::binary);media.assign(std::istreambuf_iterator<char>(f),{});assert(media.size()==131072u*512u);assert(SdBootReader::mount());auto before=waits;unsigned beforeSectors=sectorCalls;auto beforeUs=clockUs;bool ok=RuntimeInstalledProviders::loadBootstrapPackages("/sdboot","xteink-x4-pro",countedRead);assert(ok);assert(SdBootReader::release());printf("RESULT tick_us=%u readers=%u bytes=%zu sectors=%u scheduler_requests=%u transaction_waits=%u sector_waits=%u read_chunk_waits=%u filesystem_waits=%u loader_delays=%u modeled_us=%lld\n",tickUs,readerCalls,readerBytes,sectorCalls-beforeSectors,(waits["transaction"]-before["transaction"])+(waits["transfer"]-before["transfer"])+(waits["read"]-before["read"])+(waits["filesystemCheckpoint"]-before["filesystemCheckpoint"]),waits["transaction"]-before["transaction"],waits["transfer"]-before["transfer"],waits["read"]-before["read"],waits["filesystemCheckpoint"]-before["filesystemCheckpoint"],waits["ArduinoDelay"]-before["ArduinoDelay"],(long long)(clockUs-beforeUs));}
'''
(B/'probe.cpp').write_text(prefix+source+suffix)
mod=(R/'test/bootstrap_store/test.cpp').read_text();mod=mod[:mod.index('static bool hostRead(')]
(B/'verification.cpp').write_text(mod)
(B/'Arduino.h').write_text('#pragma once\n#include <cstdint>\nuint32_t millis();\nvoid delay(unsigned);\n')
flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all'] if san else []
objs=[]
for name in ['SdBootFatFs','SdBootFatUnicode']:
 obj=B/(name+'.o');subprocess.run(['cc','-std=c11','-O1','-g',*flags,'-DBOARD_XTEINK_X4_PRO','-I'+str(R/'src'),'-c',str(R/'src/platform'/(name+'.c')),'-o',str(obj)],check=True);objs.append(str(obj))
includes=['-I'+str(x) for x in [B,R/'test/bootstrap_store/stubs',R/'test/storage_volume/stubs',R/'src',R/'sdk/driver',R/'Drivers/x4pro_board',args.arduino_json.resolve()]]
subprocess.run(['c++','-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-overloaded-virtual','-Wno-error=maybe-uninitialized',*flags,*includes,str(B/'probe.cpp'),str(B/'verification.cpp'),str(R/'src/runtime/drivers/BootstrapModuleStore.cpp'),*objs,'-lcrypto','-o',str(B/'probe')],check=True)
for tick in [0,1000,10000]:subprocess.run([str(B/'probe'),str(B/'card.img'),str(tick)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=30)
