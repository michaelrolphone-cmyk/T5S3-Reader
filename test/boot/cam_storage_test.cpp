#define HAL_STORAGE_IMPL
#include <Arduino.h>
#include <HalStorage.h>
#include <esp_system.h>
#include <ff.h>
#include "../../ports/cam/SdmmcBootstrapDisk.h"
#include "../../ports/cam/HalStorageSdmmcControl.h"
#include <cassert>
#include <cstdio>
namespace BootstrapSdmmc {
static bool mounted=false, operating=false;
esp_err_t mount(Pins pins,bool write) { assert(pins.clk==39 && pins.cmd==38 && pins.d0==40 && write); mounted=true; return ESP_OK; }
esp_err_t unmount() { mounted=false; return ESP_OK; }
bool beginOperation(uint32_t,uint32_t) { if(operating || !mounted)return false; operating=true; return true; }
Progress endOperation() { operating=false; return {}; }
const char* drive() { return mounted ? "0:" : nullptr; }
void cancel() {}
bool retained() { return mounted; }
}
int main(int argc,char** argv) {
 Fake::identity=false; assert(!Storage.begin()); Fake::identity=true;
 assert(Storage.begin()); auto initial=Storage.generation(); assert(initial.quiescent);
 if(argc>1 && !strcmp(argv[1],"metadata")) {
   auto dir=Storage.open("/"); assert(dir && dir.isDirectory());
   HalFile::DirectoryEntry entry;
   std::strcpy(Fake::entry.fname,"driver.elf"); Fake::entry.fsize=1234;
   auto io=Fake::io;
   assert(dir.readDirectoryEntry(entry) && Fake::io==io+1);
   assert(!strcmp(entry.name,"driver.elf") && entry.size==1234 && !entry.isDirectory);
   Fake::entry.fattrib=AM_DIR;
   assert(dir.readDirectoryEntry(entry) && entry.isDirectory);
   Fake::entry={}; io=Fake::io;
   assert(!dir.readDirectoryEntry(entry) && !dir.getError() && !entry.name[0] && Fake::io==io+1);
   Fake::task=reinterpret_cast<void*>(2); io=Fake::io;
   assert(!dir.readDirectoryEntry(entry) && dir.getError() && Fake::io==io);
   Fake::task=reinterpret_cast<void*>(1);
   assert(!dir.getError() && dir.close());
   for(const char* invalid:{"..","a/b","a\\b"}) {
     dir=Storage.open("/"); std::strcpy(Fake::entry.fname,invalid);
     assert(!dir.readDirectoryEntry(entry) && dir.getError()==FR_INVALID_NAME && !entry.name[0]);
     Fake::entry={};io=Fake::io;
     assert(!dir.readDirectoryEntry(entry) && dir.getError() && Fake::io==io);
     assert(dir.close());
   }
   dir=Storage.open("/");std::memset(Fake::entry.fname,'x',sizeof(Fake::entry.fname));
   assert(!dir.readDirectoryEntry(entry) && dir.getError()==FR_INVALID_NAME && dir.close());
   Fake::entry={};dir=Storage.open("/");Fake::failDirectory=true;
   assert(!dir.readDirectoryEntry(entry) && dir.getError()==FR_DISK_ERR);
   Fake::failDirectory=false;io=Fake::io;
   assert(!dir.readDirectoryEntry(entry) && Fake::io==io && dir.close());
   dir=Storage.open("/");BootstrapSdmmc::operating=true;io=Fake::io;
   assert(!dir.readDirectoryEntry(entry) && dir.getError()==FR_TIMEOUT && Fake::io==io);
   BootstrapSdmmc::operating=false;assert(dir.close());
   dir=Storage.open("/");std::strcpy(Fake::entry.fname,"file");
   for(unsigned n=0;n<2048;++n)assert(dir.readDirectoryEntry(entry));
   io=Fake::io;assert(!dir.readDirectoryEntry(entry) && dir.getError()==FR_TIMEOUT && Fake::io==io);
   Fake::failDirectoryClose=true;assert(!dir.close() && !BootstrapHalStorage::releaseForHandoff());
   Fake::failDirectoryClose=false;assert(dir.close());
   dir=Storage.open("/");Storage.markUnavailable();io=Fake::io;
   assert(!dir.readDirectoryEntry(entry) && dir.getError() && Fake::io==io && dir.close());
   assert(BootstrapHalStorage::releaseForHandoff());
   puts("CAM metadata cursor: copied size/type, one read/no child open, EOF, owner/name/I/O/budget/bounds, sticky failures and retained close PASS");
   return 0;
 }
 if(argc>1 && !strcmp(argv[1],"close")) {
   { auto f=Storage.open("/file",O_RDWR); assert(f); Fake::failClose=true; }
   assert(!Storage.generation().quiescent && !BootstrapHalStorage::releaseForHandoff());
   puts("CAM failed close retains handle/mount PASS"); return 0;
 }
 if(argc>1 && !strcmp(argv[1],"stat")) {
   Fake::failStat=true; assert(!Storage.exists("/file"));
   assert(!Storage.ready() && !Storage.generation().quiescent && !BootstrapHalStorage::releaseForHandoff());
   puts("CAM stat I/O failure cannot masquerade as coherent missing inventory PASS"); return 0;
 }
 auto reader=Storage.open("/file"); assert(reader && Storage.unchanged(initial));
 auto writer=Storage.open("/file",O_RDWR); assert(writer && !Storage.generation().quiescent);
 assert(writer.write("a",1)==1); assert(writer.close()); assert(!Storage.unchanged(initial));
 auto stamp=Storage.generation(); assert(stamp.quiescent);
 Fake::task=reinterpret_cast<void*>(2); unsigned io=Fake::io; char c;
 assert(!Storage.ready() && !Storage.generation().quiescent && reader.read(&c,1)==-1);
 assert(!reader.close() && Fake::io==io);
 Fake::task=reinterpret_cast<void*>(1); assert(reader.close() && Storage.unchanged(stamp));
 assert(Storage.rename("/a","/b") && !Storage.unchanged(stamp));
 stamp=Storage.generation(); assert(BootstrapHalStorage::releaseForHandoff());
 assert(!Storage.unchanged(stamp) && Storage.begin() && !Storage.unchanged(stamp));
 Storage.externalStorageBegin(); Storage.externalStorageEnd(true);
 assert(!Storage.reconcileExternalStorage() && !Storage.generation().quiescent && !BootstrapHalStorage::releaseForHandoff());
 puts("CAM identity, owner, reader/writer generations, remount and raw-storage quarantine PASS");
}
