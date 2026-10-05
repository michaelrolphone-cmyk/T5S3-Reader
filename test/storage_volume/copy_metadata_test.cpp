#include <HalStorage.h>
#include <RiscProviderV2.h>
#include <RiscPlatformClockV1.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "os_cpu_fake.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include <mbedtls/sha256.h>
extern "C" {
uint8_t *card_image;
uint32_t card_sectors = 131072;
bool card_bad_crc, card_reject_write, card_busy_forever, card_bad_pin;
unsigned card_reads, card_writes;
bool card_sleep_off;
bool card_power_off;
unsigned card_sleep_commits;
uint64_t card_time;
unsigned inventory_sector_ms;
uint64_t sd_clock_mux_writes, sd_clock_direction_writes, sd_clock_levels;
uint32_t sd_clock_cycles;
unsigned sd_clock_cycle_calls, sd_clock_wraps;
bool sd_clock_counter_stuck, sd_clock_readback_stuck, sd_clock_check_phases;
uint32_t x4pro_sd_test_cycle_count(void) {
    ++sd_clock_cycle_calls;
    if (!sd_clock_counter_stuck) {
        const uint32_t before = sd_clock_cycles;
        sd_clock_cycles += 8;
        if (sd_clock_cycles < before) ++sd_clock_wraps;
    }
    return sd_clock_cycles;
}
const risc_driver_v2 *t5_driver_get(uint32_t);
}
static uint64_t now(void*) { return card_time; }
static unsigned sleeps;
static void sleep(void*, uint32_t ms) { ++sleeps; card_time += ms; }
static void put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void put32(uint8_t *p, uint32_t n) { for (int i=0;i<4;++i) p[i] = n >> (i*8); }
static void format(bool partitioned) {
    std::memset(card_image, 0, (size_t)card_sectors * 512);
    uint32_t base = partitioned ? 2048 : 0;
    if (base) { card_image[446+4] = 0x0c; put32(card_image+446+8,base); put32(card_image+446+12,card_sectors-base); card_image[510]=0x55;card_image[511]=0xaa; }
    uint8_t *b=card_image+(size_t)base*512;
    b[0]=0xeb; b[1]=0x58;b[2]=0x90;std::memcpy(b+3,"MSDOS5.0",8);
    put16(b+11,512); b[13]=1;put16(b+14,32);b[16]=2;b[21]=0xf8;
    put32(b+32,card_sectors-base);put32(b+36,1024);put32(b+44,2);put16(b+48,1);
    b[66]=0x29;std::memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
    for(unsigned fat=0;fat<2;++fat) { uint8_t *p=card_image+(size_t)(base+32+fat*1024)*512;put32(p,0xffffff8);put32(p+4,0xffffffff);put32(p+8,0xfffffff); }
}

// This fixture uses the actual SD driver, FatFs, HAL, package inspector and
// capability resolver. Only the card bytes and the host clock are modeled.
#include <fstream>
#include <iterator>
#include <set>
#include <string>
extern "C" int native_app_register_sd_vfs() { assert(false); return -1; }
using namespace RuntimePackages;
static const risc_storage_volume_api_v1* observedApi;
static const risc_storage_volume_api_v1_ext* observedExtension;
static unsigned volumeIoCalls;
static bool countedStat(void* context,const char* path,uint64_t* size,bool* directory) {
    ++volumeIoCalls;return observedApi->stat(context,path,size,directory);
}
static uint32_t countedDirectoryOpen(void* context,const char* path) {
    ++volumeIoCalls;return observedApi->dir_open(context,path);
}
static bool countedNext(void* context,uint32_t handle,risc_storage_dirent_v1* entry) {
    ++volumeIoCalls;return observedApi->dir_next(context,handle,entry);
}
static uint32_t countedFileOpen(void* context,const char* path,uint32_t flags) {
    ++volumeIoCalls;return observedExtension->file_open(context,path,flags);
}
static size_t countedRead(void* context,uint32_t handle,void* bytes,size_t count) {
    ++volumeIoCalls;return observedApi->file_read(context,handle,bytes,count);
}
static bool countedInfo(void* context,uint32_t handle,uint64_t* size,uint64_t* offset) {
    ++volumeIoCalls;return observedExtension->file_info(context,handle,size,offset);
}
static const PackageRuntimePolicy policy{"xtensa-esp32s3",2,8u*1024u*1024u,16u*1024u*1024u};
static uint32_t available(const char*) { return UINT32_MAX; }
struct FixtureFile { std::string path, bytes; };
static void write(const std::string& path,const std::string& bytes) {
    auto file=Storage.open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC); assert(file);
    assert(file.write(bytes.data(),bytes.size())==bytes.size()); assert(file.close());
}
static bool inspect(const std::string& root,OrdinaryInspectionDiagnostic* diagnostic=nullptr) {
    Identity identity{}; OrdinaryPackagePlan plan;
    return inspectInstalledOrdinarySdDirectory(root.c_str(),policy,available,identity,&plan,diagnostic);
}
static void capabilities() {
    auto* snapshot=captureInstalledCapabilities();
    assert(snapshot && versionInInstalledSnapshot(snapshot,"board.battery")==1 &&
           versionInInstalledSnapshot(snapshot,"rtc.clock")==2);
    releaseInstalledCapabilities(snapshot);
}
static std::string appleDouble() {
    // AppleDouble v2 with one empty resource-fork entry. The installed-package
    // rule classifies the filename/type; these bytes model an actual sidecar.
    std::string bytes(38,'\0');
    const unsigned char header[]={0,5,22,7,0,2,0,0};
    std::memcpy(bytes.data(),header,sizeof(header));
    bytes[25]=1; bytes[29]=2; bytes[33]=38;
    return bytes;
}
int main(int argc,char** argv) {
    assert(argc==4 || argc==5);
    const unsigned sectorMs=static_cast<unsigned>(std::strtoul(argv[3],nullptr,10));
    assert(sectorMs && sectorMs<=8);
    const bool expectRejection=argc==5 && !std::strcmp(argv[4],"--expect-rejection");
    std::ifstream listing(argv[2]); assert(listing);
    std::vector<FixtureFile> files; std::set<std::string> roots;
    for(std::string path;std::getline(listing,path);) {
        assert(path.rfind("/Drivers/",0)==0);
        std::ifstream input(std::string(argv[1])+path,std::ios::binary); assert(input);
        files.push_back({path,std::string(std::istreambuf_iterator<char>(input),{})});
        roots.insert(path.substr(0,path.find_last_of('/')));
    }
    assert(files.size()==roots.size()*5 && roots.size()>=9 && roots.size()<=64);
    card_image=static_cast<uint8_t*>(std::calloc(card_sectors,512)); assert(card_image);format(false);
    const auto* driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    const auto* api=static_cast<const risc_storage_volume_api_v1*>(driver->capability);
    risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,now,sleep};
    risc_provider_dependency_v1 dependencies[]={{"platform.clock",1,&clock}};
    assert(driver->start(dependencies,1));
    observedApi=api;observedExtension=risc_storage_volume_extension(api);assert(observedExtension);
    auto counted=*observedExtension;counted.base.struct_size=sizeof(counted);
    counted.base.stat=countedStat;counted.base.dir_open=countedDirectoryOpen;counted.base.dir_next=countedNext;
    counted.file_open=countedFileOpen;counted.base.file_read=countedRead;counted.file_info=countedInfo;
    assert(Storage.bindVolume(&counted.base));
    assert(Storage.mkdir("/Drivers"));
    for(const auto& root:roots) assert(Storage.mkdir(root.c_str()));
    for(const auto& file:files) write(file.path,file.bytes);
    for(const auto& root:roots) assert(inspect(root));
    capabilities();
    const auto sidecar=appleDouble();
    for(const auto& file:files) {
        const size_t slash=file.path.find_last_of('/');
        write(file.path.substr(0,slash+1)+"._"+file.path.substr(slash+1),sidecar);
    }
    for(const auto& root:roots) write("/Drivers/._"+root.substr(9),sidecar);
    unsigned rejected=0;
    for(const auto& root:roots) {
        OrdinaryInspectionDiagnostic diagnostic;
        if(!inspect(root,&diagnostic)) {
            ++rejected;
            std::fprintf(stderr,"copy inspection rejected %s stage=%s entry=%s\n",root.c_str(),diagnostic.stage,diagnostic.entry);
            if(expectRejection) assert(!std::strcmp(diagnostic.stage,"initial-tree"));
        }
    }
    if(expectRejection) {
        assert(rejected==roots.size());
        auto* snapshot=captureInstalledCapabilities();
        assert(!versionInInstalledSnapshot(snapshot,"board.battery") &&
               !versionInInstalledSnapshot(snapshot,"rtc.clock"));
        releaseInstalledCapabilities(snapshot);
        assert(driver->quiesce());driver->stop();std::free(card_image);
        std::printf("Baseline: %zu valid packages rejected after regular AppleDouble copy companions; battery=0 rtc=0 PASS\n",roots.size());
        return 0;
    }
    assert(!rejected);
    for(const auto& root:roots)write(root+"/.DS_Store","Finder metadata");
    write("/Drivers/.DS_Store","Finder metadata");
    for(const auto& root:roots)assert(inspect(root));
    const auto hashBefore=receiptTestHashBytes;
    inventory_sector_ms=sectorMs;Storage.invalidateObservations();
    const auto began=card_time;const auto reads=card_reads;
    capabilities();
    const auto elapsed=card_time-began;const auto sectors=card_reads-reads;
    assert(elapsed<15000 && receiptTestHashBytes==hashBefore);
    const auto warmReads=card_reads,warmWrites=card_writes,warmCalls=volumeIoCalls;
    for(unsigned i=0;i<10;++i)capabilities();
    assert(card_reads==warmReads && card_writes==warmWrites && volumeIoCalls==warmCalls);
    std::fprintf(stderr,"Copied inventory: %zu providers, %u ms/sector, %u sectors, %llu modeled ms; ten warm calls zero volume/sector I/O\n",roots.size(),sectorMs,sectors,(unsigned long long)elapsed);
    inventory_sector_ms=0;
    const std::string battery="/Drivers/x4pro-battery";
    assert(roots.count(battery));
    // Ordinary unknown files remain forbidden. AppleDouble ._* files are inert
    // installed-tree metadata and may be ignored without becoming members.
    for(const char* name:{"unknown",".hidden"}) {
        const auto path=battery+"/"+name;write(path,sidecar);
        assert(!inspect(battery));assert(Storage.remove(path.c_str()));assert(inspect(battery));
    }
    const auto unknownCompanion=battery+"/._unknown";
    write(unknownCompanion,sidecar);assert(inspect(battery));
    assert(Storage.remove(unknownCompanion.c_str()));assert(inspect(battery));
    // A copy-looking directory is not a regular sidecar and is never traversed.
    const auto fakeDirectory=battery+"/._driver.elf";
    assert(Storage.remove(fakeDirectory.c_str()));assert(Storage.mkdir(fakeDirectory.c_str()));
    assert(!inspect(battery));assert(Storage.rmdir(fakeDirectory.c_str()));write(fakeDirectory,sidecar);
    const auto finderDirectory=battery+"/.DS_Store";
    assert(Storage.remove(finderDirectory.c_str()));assert(Storage.mkdir(finderDirectory.c_str()));
    assert(!inspect(battery));assert(Storage.rmdir(finderDirectory.c_str()));write(finderDirectory,"Finder metadata");
    // A companion cannot stand in for a missing declaration, including imports.
    for(const char* name:{"driver.elf","privileged-imports.v1","provider-abi.v1","manifest.json",".package.json"}) {
        const auto path=battery+"/"+name;
        const FixtureFile* original=nullptr;for(const auto& file:files)if(file.path==path)original=&file;
        assert(original && Storage.remove(path.c_str()));assert(!inspect(battery));
        write(path,original->bytes);assert(inspect(battery));
    }
    // Independently retain declared size and ELF-header validation.
    for(const char* name:{"driver.elf","privileged-imports.v1"}) {
        const auto path=battery+"/"+name;
        const FixtureFile* original=nullptr;for(const auto& file:files)if(file.path==path)original=&file;
        assert(original);write(path,original->bytes+"x");assert(!inspect(battery));
        write(path,original->bytes);
    }
    const FixtureFile* elf=nullptr;for(const auto& file:files)if(file.path==battery+"/driver.elf")elf=&file;
    assert(elf);auto badElf=elf->bytes;badElf[0]=0;write(elf->path,badElf);
    assert(!inspect(battery));write(elf->path,elf->bytes);assert(inspect(battery));
    // Slow real FAT-sector progress is still finite and may not publish a prefix.
    Storage.invalidateObservations();inventory_sector_ms=1000;
    assert(!captureInstalledCapabilities());inventory_sector_ms=0;capabilities();
    // Nonmetadata entries retain their original 64-entry limit, independently
    // of the bounded copy-metadata allowance. An exact bound needs clean EOF.
    const unsigned padding=64-static_cast<unsigned>(roots.size());
    for(unsigned i=0;i<padding;++i)write("/Drivers/unowned-"+std::to_string(i),"unowned");
    capabilities();write("/Drivers/unowned-overflow","unowned");
    assert(!captureInstalledCapabilities());assert(Storage.remove("/Drivers/unowned-overflow"));
    for(unsigned i=0;i<padding;++i)assert(Storage.remove(("/Drivers/unowned-"+std::to_string(i)).c_str()));
    capabilities();
    // Raw entries, including ignored copy-like root files, retain a hard cap.
    const unsigned extraMetadata=65-static_cast<unsigned>(roots.size());
    for(unsigned i=0;i<extraMetadata;++i)write("/Drivers/._unowned-"+std::to_string(i),sidecar);
    assert(!captureInstalledCapabilities());
    for(unsigned i=0;i<extraMetadata;++i)assert(Storage.remove(("/Drivers/._unowned-"+std::to_string(i)).c_str()));
    capabilities();
    // Explicit integrity verification remains byte-sensitive. Clear only the
    // fixture-created sidecars so the strict transaction verifier can run too.
    for(const auto& file:files) {
        const size_t slash=file.path.find_last_of('/');
        assert(Storage.remove((file.path.substr(0,slash+1)+"._"+file.path.substr(slash+1)).c_str()));
    }
    for(const auto& root:roots)assert(Storage.remove((root+"/.DS_Store").c_str()));
    Identity identity{};assert(verifyOrdinarySdDirectory(battery.c_str(),policy,available,identity));
    const FixtureFile* imports=nullptr;for(const auto& file:files)if(file.path==battery+"/privileged-imports.v1")imports=&file;
    assert(imports && !imports->bytes.empty());auto changed=imports->bytes;changed[0]^=1;write(imports->path,changed);
    assert(!verifyOrdinarySdDirectory(battery.c_str(),policy,available,identity));write(imports->path,imports->bytes);
    assert(verifyOrdinarySdDirectory(battery.c_str(),policy,available,identity));
    assert(driver->quiesce());driver->stop();std::free(card_image);
    std::printf("Copy metadata: %zu providers at %u ms/sector, %u sectors/%llu modeled ms; battery=1 rtc=2, warm zero volume/sector I/O, strict declared members/hashes, unknown refusal and finite bounds PASS\n",roots.size(),sectorMs,sectors,(unsigned long long)elapsed);
}
