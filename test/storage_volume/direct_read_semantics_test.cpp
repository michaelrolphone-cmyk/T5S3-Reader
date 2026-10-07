#include "card.inc"
#include <map>
#include <cerrno>
#include <fcntl.h>
#include <esp_vfs.h>
#include <freertos/semphr.h>
#include "native/ManagedAppAdmission.h"
#include "runtime/resources/ExecutionContext.h"
#include "runtime/packages/PackageExecutableAdmission.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageVerificationReceiptSd.h"
using namespace RuntimePackages;
bool testRegular(const char*); bool testBytes(const char*,uint64_t,const char*,bool);
bool testMetadata(const std::string&,size_t,std::string&,bool&);
extern "C" int native_app_register_sd_vfs();
const char* native_app_current_path(){return nullptr;}
namespace RuntimePackages {bool newPackageReceiptGeneration(uint8_t (&v)[16]){std::memset(v,1,16);return true;}}
static const risc_storage_volume_api_v1* api;
static const risc_storage_volume_api_v1_ext* ext;
static std::map<uint32_t,std::string> filePaths,dirPaths;
static unsigned stats,opens,diropens,reads,closes;
static std::string badClose,badRead,falseStat,badOpen;
static unsigned closeBudget, denyReady;
static bool readyHook(void* c){if(denyReady){--denyReady;return false;}return api->ready(c);}
static uint32_t lastFailedHandle;
static uint32_t openHook(void* c,const char* p,uint32_t f){++opens;if(badOpen==p&&!badOpen.empty())return 0;const auto h=ext->file_open(c,p,f);if(h)filePaths[h]=p;return h;}
static uint32_t dirHook(void* c,const char* p){++diropens;const auto h=api->dir_open(c,p);if(h)dirPaths[h]=p;return h;}
static bool statHook(void* c,const char* p,uint64_t* n,bool* d){++stats;if(falseStat==p)return false;return api->stat(c,p,n,d);}
static size_t readHook(void* c,uint32_t h,void* b,size_t n){++reads;if(filePaths[h]==badRead&&!badRead.empty())return 0;return api->file_read(c,h,b,n);}
static uint32_t errorHook(void* c,uint32_t h,bool d){if(!d&&filePaths[h]==badRead&&!badRead.empty())return 1;return ext->handle_error(c,h,d);}
static bool closeHook(void* c,uint32_t h,bool commit){++closes;if(closeBudget&&filePaths[h]==badClose){--closeBudget;lastFailedHandle=h;return false;}const bool ok=api->file_close(c,h,commit);if(ok)filePaths.erase(h);return ok;}
static bool dirCloseHook(void* c,uint32_t h){++closes;if(closeBudget&&dirPaths[h]==badClose){--closeBudget;lastFailedHandle=h;return false;}const bool ok=ext->dir_close_checked(c,h);if(ok)dirPaths.erase(h);return ok;}
static void write(const std::string& p,const std::string& b){auto f=Storage.open(p.c_str(),O_WRONLY|O_CREAT|O_TRUNC);assert(f);assert(f.write(b.data(),b.size())==b.size()&&f.close());}
static std::string hex(const uint8_t* b){std::string r;for(unsigned i=0;i<32;++i){r+=' ' ; r.back()="0123456789abcdef"[b[i]>>4];r+="0123456789abcdef"[b[i]&15];}return r;}
static std::string digest(const std::string& s){uint8_t d[32]{};assert(packageSnapshotDigest((const uint8_t*)s.data(),s.size(),d));return hex(d);}
static std::string elf(){std::string b(64,0);std::memcpy(b.data(),"\x7f" "ELF\x01\x01",6);b[6]=1;b[16]=3;b[18]=94;b[20]=1;return b;}
struct Count{unsigned s=stats,o=opens,d=diropens,r=reads,c=closes,sectors=card_reads;void show(const char* n,bool result){if(std::getenv("DIRECT_READ_REQUIRE_ZERO_STATS")){const std::string label(n);if(label.rfind("regular:",0)==0||label.rfind("bytes-",0)==0||label=="metadata"||label=="managed-begin")assert(stats==s&&"direct-read stat budget");if(label.rfind("admission-",0)==0)assert(stats-s==1&&"direct-read stat budget");}std::printf("RESULT %s=%d stats=%u opens=%u dirs=%u reads=%u closes=%u sectors=%u live=%zu quiescent=%d\n",n,result,stats-s,opens-o,diropens-d,reads-r,closes-c,card_reads-sectors,filePaths.size()+dirPaths.size(),Storage.generation().quiescent);}};
static Identity id{};static OrdinaryPackagePlan plan{};static std::string manifest,bytes,sha;static uint8_t md[32]{},ed[32]{};
static void makePackage(){
 assert(Storage.mkdir("/Apps")&&Storage.mkdir("/Apps/reader"));bytes=elf();sha=digest(bytes);
 assert(makeIdentity(Kind::Application,"reader","1.0.0","reader.elf",false,&id));
 const std::string sidecar="{\"file_name\":\"reader.elf\"}";
 manifest="{\"schema\":1,\"kind\":\"application\",\"id\":\"reader\",\"version\":\"1.0.0\",\"artifact\":\"reader.elf\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,\"entries\":[{\"name\":\"reader.elf\",\"size_bytes\":64,\"sha256\":\""+sha+"\",\"executable\":true},{\"name\":\"reader.json\",\"size_bytes\":"+std::to_string(sidecar.size())+",\"sha256\":\""+digest(sidecar)+"\",\"executable\":false}],\"requires\":[]}";
 assert(parseOrdinaryManifest(manifest.data(),manifest.size(),plan));
 assert(packageSnapshotDigest((const uint8_t*)manifest.data(),manifest.size(),md));assert(packageSnapshotDigest((const uint8_t*)bytes.data(),bytes.size(),ed));
 write("/Apps/reader/.package.json",manifest);write("/Apps/reader/reader.elf",bytes);write("/Apps/reader/reader.json",sidecar);
 assert(systemPackageUseGate().pin("/Apps/reader"));
}
static bool admit(StorageGenerationStamp stamp){return admitInstalledExecutableSnapshot(id,md,ed,(const uint8_t*)bytes.data(),bytes.size(),stamp);}
int main(int argc,char** argv){
 std::setbuf(stdout,nullptr);assert(argc==2);const std::string scenario=argv[1];
 card_image=(uint8_t*)std::calloc(card_sectors,512);assert(card_image);format(false);
 const auto* driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);api=(const risc_storage_volume_api_v1*)driver->capability;ext=risc_storage_volume_extension(api);
 risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,now,sleep};risc_provider_dependency_v1 deps[]={{"platform.clock",1,&clock}
#ifdef TEST_SPI_TRANSPORT
 ,{"spi.bus",1,&SpiCardFixture::api}
#endif
 };assert(driver->start(deps,sizeof(deps)/sizeof(deps[0])));
 auto hooks=*ext;hooks.base.struct_size=sizeof(hooks);hooks.base.ready=readyHook;hooks.file_open=openHook;hooks.base.dir_open=dirHook;hooks.base.stat=statHook;hooks.base.file_read=readHook;hooks.base.file_close=closeHook;hooks.dir_close_checked=dirCloseHook;hooks.handle_error=errorHook;assert(Storage.bindVolume(&hooks.base));
 assert(Storage.mkdir("/dir"));write("/file",elf());write("/empty","");write("/meta","metadata");makePackage();
 bool uncertain=false;std::string out;
 if(scenario=="normal"){
  for(const char* p:{"/file","/empty","/missing","/dir"}){Count c;const bool v=testRegular(p);assert(v==(!std::strcmp(p,"/file")||!std::strcmp(p,"/empty")));c.show(("regular:"+std::string(p)).c_str(),v);}
  for(bool full:{false,true}){Count c;assert(testBytes("/file",64,sha.c_str(),full));c.show(full?"bytes-full":"bytes-header",true);}
  {Count c;assert(testMetadata("/meta",64,out,uncertain)&&out=="metadata"&&!uncertain);c.show("metadata",true);}
  {Count c;assert(admit(Storage.generation()));c.show("admission-cold",true);}
  {Count c;const auto n=receiptTestHashBytes;assert(admit(Storage.generation()));assert(receiptTestHashBytes-n<bytes.size()+manifest.size());c.show("admission-warm",true);}
  RuntimeResources::ExecutionContext ctx;assert(ctx.begin());{Count c;assert(beginManagedAppAdmission(id,"/sd/Apps/reader/reader.elf"));c.show("managed-begin",true);}endManagedAppAdmission();ctx.end();
  assert(native_app_register_sd_vfs()==ESP_OK);{Count c;int fd=captured.open("/file",O_RDONLY,0);assert(fd==0);char b[64];assert(captured.read(fd,b,64)==64&&!std::memcmp(b,bytes.data(),64));assert(captured.close(fd)==0);c.show("vfs",true);}
 }else if(scenario=="reject"){
  for(const char* p:{"/empty","/missing","/dir"}){Count c;assert(!testBytes(p,64,sha.c_str(),false));assert(!testMetadata(p,64,out,uncertain)&&!uncertain);c.show(p,false);}
  auto bad=elf();bad[0]=0;write("/bad",bad);assert(!testBytes("/bad",64,sha.c_str(),false));write("/bad",elf());assert(!testBytes("/bad",63,sha.c_str(),false));assert(!testBytes("/bad",64,std::string(64,'0').c_str(),true));assert(!testMetadata("/meta",2,out,uncertain));
  write("/Apps/reader/.package.json","invalid");assert(!admit(Storage.generation()));write("/Apps/reader/.package.json",manifest);assert(admit(Storage.generation()));
  assert(Storage.remove("/Apps/reader/.package.json"));assert(!admit(Storage.generation()));assert(Storage.mkdir("/Apps/reader/.package.json"));assert(!admit(Storage.generation()));assert(Storage.rmdir("/Apps/reader/.package.json"));write("/Apps/reader/.package.json",manifest);assert(admit(Storage.generation()));
  assert(native_app_register_sd_vfs()==ESP_OK);for(const char* p:{"/empty","/missing","/dir"}){assert(captured.open(p,O_RDONLY,0)==-1);assert(errno==(!std::strcmp(p,"/empty")?EFBIG:ENOENT));}assert(captured.open("/file",O_WRONLY,0)==-1&&errno==EROFS);
  std::puts("CONTROL malformed/header/size/digest/metadata rejects and clean retry PASS");
 }else if(scenario=="open-error"){
  badOpen="/file";assert(!testRegular("/file")&&!testBytes("/file",64,sha.c_str(),false));badOpen="/meta";assert(!testMetadata("/meta",64,out,uncertain)&&!uncertain);badOpen="/Apps/reader/.package.json";assert(!admit(Storage.generation()));badOpen.clear();assert(testRegular("/file")&&admit(Storage.generation()));std::puts("CONTROL open failure rejects and retry PASS");
 }else if(scenario=="exhaustion"){
  uint32_t handles[64]{};size_t n=0;while(n<64){auto h=ext->file_open(nullptr,"/file",RISC_STORAGE_OPEN_READ);if(!h)break;handles[n++]=h;}assert(n&&n<64);assert(!testRegular("/file")&&!testMetadata("/meta",64,out,uncertain)&&!admit(Storage.generation()));for(size_t i=0;i<n;++i)assert(api->file_close(nullptr,handles[i],true));assert(testRegular("/file")&&admit(Storage.generation()));std::puts("CONTROL actual provider handle exhaustion rejects and retry PASS");
 }else if(scenario=="close-bytes"){
  for(bool full:{false,true}){badClose="/file";closeBudget=1;assert(!testBytes("/file",64,sha.c_str(),full));assert(!closeBudget&&filePaths.empty()&&!Storage.generation().quiescent&&!Storage.begin());}std::puts("CONTROL header/full-SHA close failures reject and preserve uncertainty PASS");
 }else if(scenario=="read-error"){
  badRead="/file";assert(!testBytes("/file",64,sha.c_str(),false));badRead="/meta";assert(!testMetadata("/meta",64,out,uncertain)&&!uncertain);badRead="/Apps/reader/.package.json";assert(!admit(Storage.generation()));badRead.clear();assert(testBytes("/file",64,sha.c_str(),true)&&admit(Storage.generation()));std::puts("CONTROL read error rejection and retry PASS");
 }else if(scenario=="close-file"){
  badClose="/file";closeBudget=1;assert(!testRegular("/file"));assert(!closeBudget&&!Storage.generation().quiescent&&!Storage.begin());assert(filePaths.empty());std::puts("CONTROL rejected regular close; destructor retry releases handle but preserves uncertainty PASS");
 }else if(scenario=="close-metadata"){
  badClose="/Apps/reader/.package.json";closeBudget=1;RuntimeResources::ExecutionContext ctx;assert(ctx.begin());assert(!beginManagedAppAdmission(id,"/sd/Apps/reader/reader.elf"));assert(systemPackageUseGate().unpin("/Apps/reader")&&systemPackageUseGate().pinned("/Apps/reader"));assert(!Storage.generation().quiescent&&!Storage.begin());assert(filePaths.empty());ctx.end();std::puts("CONTROL metadata close failure retains package pin and poisons reuse PASS");
 }else if(scenario=="close-admission"){
  badClose="/Apps/reader/.package.json";closeBudget=1;assert(!admit(Storage.generation()));assert(!closeBudget&&!Storage.generation().quiescent&&!Storage.begin());assert(filePaths.empty());std::puts("CONTROL snapshot metadata close failure rejects and remains nonquiescent PASS");
 }else if(scenario=="close-vfs"){
  assert(native_app_register_sd_vfs()==ESP_OK);int fd=captured.open("/file",O_RDONLY,0);assert(fd==0);badClose="/file";closeBudget=1;assert(captured.close(fd)==-1&&errno==EIO&&filePaths.size()==1);assert(!Storage.begin()&&!driver->quiesce());assert(captured.close(fd)==0&&!filePaths.size()&&!Storage.generation().quiescent&&!Storage.begin());std::puts("CONTROL VFS failed close retains slot, same-handle retry, uncertainty survives PASS");
 }else if(scenario=="stale"){
  HalFile f=Storage.open("/file");assert(f);badClose="/file";closeBudget=1;const auto op=opens;assert(!Storage.openFileForRead("test","/meta",f));assert(f.isOpen()&&opens==op&&filePaths.size()==1);char name[30]{};f.getName(name,sizeof(name));assert(!std::strcmp(name,"file")&&!Storage.begin()&&!driver->quiesce());assert(f.close()&&!Storage.generation().quiescent&&!Storage.begin());std::puts("CONTROL direct API refuses replacing retained destination; same-handle retry PASS");
 }else if(scenario=="stale-vfs"){
  assert(native_app_register_sd_vfs()==ESP_OK);int fd=captured.open("/file",O_RDONLY,0);assert(fd==0);
  badClose="/file";closeBudget=1;denyReady=1;const auto op=opens;
  const int replacement=captured.open("/meta",O_RDONLY,0);
  std::printf("CONTROL stale-vfs replacement=%d errno=%d opens=%u live=%zu uncertainty=%d\n",replacement,errno,opens-op,filePaths.size(),!Storage.generation().quiescent);
  assert(!denyReady&&!closeBudget&&!Storage.generation().quiescent);
  if(std::getenv("VFS_REQUIRE_STALE_REJECTION"))assert(replacement==-1&&"existing VFS must reject stale destination");
  if(replacement==0){char first=0;assert(captured.read(0,&first,1)==1&&first==0x7f);assert(captured.close(0)==0);std::puts("CONTROL baseline returned old executable for replacement metadata path");}
  else{assert(replacement==-1&&errno==ENOENT&&filePaths.empty());std::puts("CONTROL prototype rejects replacement and preserves uncertainty");}
 }else if(scenario=="wrongtype-close"){
  badClose="/dir";closeBudget=1;Count c;assert(!testMetadata("/dir",64,out,uncertain));c.show("directory-metadata",false);std::printf("CONTROL wrongtype uncertain=%d consumed_close_fault=%d live=%zu\n",uncertain,!closeBudget,filePaths.size()+dirPaths.size());
 }else if(scenario=="receipt"){
  PackageVerificationReceipt receipt{};assert(readPackageReceipt("/Apps/reader",plan,(const uint8_t*)manifest.data(),manifest.size(),receipt)==ReceiptReadResult::Missing);
  assert(writeVerifiedStageReceipt("/Apps/reader",plan,(const uint8_t*)manifest.data(),manifest.size())==ReceiptWriteResult::Complete);
  {Count c;assert(readPackageReceipt("/Apps/reader",plan,(const uint8_t*)manifest.data(),manifest.size(),receipt)==ReceiptReadResult::Matched);c.show("receipt-matched",true);}
  write("/Apps/reader/.package.receipt","forged");assert(readPackageReceipt("/Apps/reader",plan,(const uint8_t*)manifest.data(),manifest.size(),receipt)==ReceiptReadResult::Invalid);
  assert(Storage.remove("/Apps/reader/.package.receipt")&&Storage.mkdir("/Apps/reader/.package.receipt"));badClose="/Apps/reader/.package.receipt";closeBudget=1;assert(readPackageReceipt("/Apps/reader",plan,(const uint8_t*)manifest.data(),manifest.size(),receipt)==ReceiptReadResult::CloseUncertain);assert(!closeBudget&&!Storage.generation().quiescent);std::puts("CONTROL unchanged receipt Missing/Matched/Invalid/wrong-type CloseUncertain PASS");
 }else if(scenario=="stat-false"){
  falseStat="/file";Count c;assert(testRegular("/file"));c.show("stat-false-open-success",true);
 }else if(scenario=="media-error"){
  assert(Storage.begin());card_bad_crc=true;assert(!testRegular("/file")&&!Storage.ready());card_bad_crc=false;assert(Storage.begin()&&testRegular("/file"));std::puts("CONTROL real media CRC failure rejects; remount retry works PASS");
 }else if(scenario=="coherency"){
  auto stamp=Storage.generation();assert(admit(stamp));Storage.invalidateObservations();const auto n=receiptTestHashBytes;assert(admit(stamp)&&receiptTestHashBytes-n>=manifest.size()+bytes.size());bytes.back()^=1;assert(!admit(Storage.generation()));bytes.back()^=1;
  Storage.externalStorageBegin();assert(admit(Storage.generation()));Storage.externalStorageEnd(true);assert(Storage.reconcileExternalStorage());assert(admit(Storage.generation()));std::puts("CONTROL stale epoch full hash, corruption rejection, raw-access and reconcile PASS");
 }else if(scenario=="vfs-reject-close"){
  assert(native_app_register_sd_vfs()==ESP_OK);badClose="/empty";closeBudget=1;assert(captured.open("/empty",O_RDONLY,0)==-1&&errno==EFBIG);assert(filePaths.size()==1&&!Storage.begin()&&!driver->quiesce());assert(captured.close(0)==0&&filePaths.empty()&&!Storage.generation().quiescent);std::puts("CONTROL invalid-size VFS failed close retains slot and retry PASS");
 }else assert(false);
 assert(filePaths.empty()&&dirPaths.empty()&&FakeLock::held==0);assert(driver->quiesce());driver->stop();std::free(card_image);std::printf("PASS %s\n",scenario.c_str());
}
