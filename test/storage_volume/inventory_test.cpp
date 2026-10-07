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
#ifdef TEST_APP_PARSER
#include "native/AppManifest.h"
#endif
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
#ifdef TEST_SPI_TRANSPORT
#include "spi_card_model.h"
#endif
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

static const risc_storage_volume_api_v1* inspectorApi;
static const risc_storage_volume_api_v1_ext* inspectorExt;
static uint32_t inspectedHeader, inspectedManifest;
static bool changeFinalSize, failManifestClose;
static unsigned manifestCloseAttempts;
static uint32_t inspectorOpen(void* context,const char* path,uint32_t flags) {
    const auto handle=inspectorExt->file_open(context,path,flags);
    if(!std::strcmp(path,"/Drivers/test-provider-39/driver.elf"))inspectedHeader=handle;
    if(!std::strcmp(path,"/Drivers/test-provider-39/.package.json"))inspectedManifest=handle;
    return handle;
}
static size_t inspectorRead(void* context,uint32_t handle,void* bytes,size_t count) {
    const auto received=inspectorApi->file_read(context,handle,bytes,count);
    if(received && handle==inspectedHeader && changeFinalSize) {
        changeFinalSize=false;
        const auto changed=inspectorExt->file_open(context,"/Drivers/test-provider-39/privileged-imports.v1",RISC_STORAGE_OPEN_WRITE|RISC_STORAGE_OPEN_TRUNCATE);
        assert(changed && inspectorApi->file_write(context,changed,"xx",2)==2);
        assert(inspectorApi->file_close(context,changed,true));
    }
    return received;
}
static bool inspectorClose(void* context,uint32_t handle,bool commit) {
    if(handle==inspectedManifest) {
        ++manifestCloseAttempts;
        if(failManifestClose) {failManifestClose=false;return false;}
    }
    return inspectorApi->file_close(context,handle,commit);
}
extern "C" int native_app_register_sd_vfs() { assert(false); return -1; }
using namespace RuntimePackages;
static std::string packagePath(unsigned index) {
    return "/Drivers/test-provider-" + std::to_string(index);
}
static std::string elfBytes() {
    std::string elf(64, '\0'); std::memcpy(elf.data(), "\x7f" "ELF\x01\x01", 6);
    elf[6]=1; elf[16]=3; elf[18]=94; elf[20]=1; return elf;
}
static void install(unsigned index) {
    const std::string id="test-provider-"+std::to_string(index), path=packagePath(index);
    const std::string capability=index==38?"board.battery":index==39?"rtc.clock":"test.provider"+std::to_string(index);
    const std::string profile="os-cpu-abi=1\nprovides="+capability+"\napi="+(index==39?"2":"1")+"\n";
    const std::string source="{\"os_cpu_abi\":1}", imports="\n", elf=elfBytes(), hash(64,'a');
    const std::pair<const char*,std::string> entries[]={{"driver.elf",elf},{"provider-abi.v1",profile},{"manifest.json",source},{"privileged-imports.v1",imports}};
    if (!Storage.exists(path.c_str())) assert(Storage.mkdir(path.c_str()));
    std::string manifest="{\"schema\":1,\"kind\":\"driver\",\"id\":\""+id+"\",\"version\":\"1.0.0\",\"artifact\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,\"entries\":[";
    for (unsigned n=0;n<4;++n) {
        if(n)manifest+=",";
        manifest+="{\"name\":\""+std::string(entries[n].first)+"\",\"size_bytes\":"+std::to_string(entries[n].second.size())+",\"sha256\":\""+hash+"\",\"executable\":"+(n==0?"true":"false")+"}";
        auto file=Storage.open((path+"/"+entries[n].first).c_str(),O_WRONLY|O_CREAT|O_TRUNC); assert(file);
        assert(file.write(entries[n].second.data(),entries[n].second.size())==entries[n].second.size());assert(file.close());
    }
    manifest+="],\"requires\":[]}"; assert(Storage.writeFile((path+"/.package.json").c_str(),manifest.c_str()));
}
int main() {
    card_image=static_cast<uint8_t*>(std::calloc(card_sectors,512)); assert(card_image); format(false);
    const auto* driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    const auto* api=static_cast<const risc_storage_volume_api_v1*>(driver->capability);
    risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,now,sleep};
    risc_provider_dependency_v1 dependencies[]={{"platform.clock",1,&clock}};
    assert(driver->start(dependencies,1) && Storage.bindVolume(api));
    assert(Storage.mkdir("/Drivers")); for(unsigned i=0;i<40;++i)install(i);
    Identity setupIdentity{};
    const PackageRuntimePolicy setupPolicy{"xtensa-esp32s3",2,8u*1024u*1024u,16u*1024u*1024u};
    assert(inspectInstalledOrdinarySdDirectory(packagePath(39).c_str(),setupPolicy,[](const char*)->uint32_t{return UINT32_MAX;},setupIdentity));
    inventory_sector_ms=3; const auto began=card_time; const auto reads=card_reads;
    auto* snapshot=captureInstalledCapabilities();
    std::fprintf(stderr,"inventory result=%d battery=%u rtc=%u elapsed=%llu sectors=%u\n",snapshot!=nullptr,versionInInstalledSnapshot(snapshot,"board.battery"),versionInInstalledSnapshot(snapshot,"rtc.clock"),(unsigned long long)(card_time-began),card_reads-reads);
    assert(snapshot && versionInInstalledSnapshot(snapshot,"board.battery")==1 && versionInInstalledSnapshot(snapshot,"rtc.clock")==2);
    const auto elapsed=card_time-began; const auto sectors=card_reads-reads;
    assert(elapsed<15000 && sectors<4500 && receiptTestHashBytes==0);
    const auto warmReads=card_reads;auto* warm=captureInstalledCapabilities();assert(warm && card_reads==warmReads);
    releaseInstalledCapabilities(warm);releaseInstalledCapabilities(snapshot);
    Storage.invalidateObservations();inventory_sector_ms=8;
    assert(!captureInstalledCapabilities()); // Actual finite deadline remains effective.
    inventory_sector_ms=0;
    // Same-length bad ELF header, wrong payload size and malformed declared
    // metadata cannot enter the capability snapshot or its cache.
    for(unsigned fault=0;fault<3;++fault) {
        install(39);const auto path=packagePath(39);
        if(fault==0){auto bytes=elfBytes();bytes[0]=0;auto f=Storage.open((path+"/driver.elf").c_str(),O_WRONLY|O_TRUNC);assert(f);assert(f.write(bytes.data(),bytes.size())==bytes.size());assert(f.close());}
        if(fault==1)assert(Storage.writeFile((path+"/driver.elf").c_str(),"short"));
        if(fault==2)assert(Storage.writeFile((path+"/provider-abi.v1").c_str(),"invalid"));
        // Diagnostic capture must preserve admission and perform precisely
        // the same SD reads as the existing silent inspection.
        OrdinaryPackagePlan inspected;
        OrdinaryInspectionDiagnostic diagnostic;
        auto inspectedReads=card_reads;
        assert(!inspectInstalledOrdinarySdDirectory(path.c_str(),setupPolicy,[](const char*)->uint32_t{return UINT32_MAX;},setupIdentity,&inspected));
        const auto silentReads=card_reads-inspectedReads;inspectedReads=card_reads;
        assert(!inspectInstalledOrdinarySdDirectory(path.c_str(),setupPolicy,[](const char*)->uint32_t{return UINT32_MAX;},setupIdentity,&inspected,&diagnostic));
        assert(card_reads-inspectedReads==silentReads);
        assert(!std::strcmp(diagnostic.stage,fault==0?"elf-header":"initial-tree"));
        const char* rejected=fault==2?"provider-abi.v1":"driver.elf";
        assert(!std::strcmp(diagnostic.entry,rejected));
        auto* bad=captureInstalledCapabilities();assert(bad && !versionInInstalledSnapshot(bad,"rtc.clock") && versionInInstalledSnapshot(bad,"board.battery")==1);
        releaseInstalledCapabilities(bad);const auto rejectedReads=card_reads;
        bad=captureInstalledCapabilities();assert(bad && card_reads>rejectedReads);releaseInstalledCapabilities(bad);
    }
    // An undeclared file is not part of the provider package and cannot make
    // an otherwise valid installed provider unavailable.
    install(39);{
        const auto path=packagePath(39);
        assert(Storage.writeFile((path+"/unexpected").c_str(),"unowned"));
        OrdinaryPackagePlan inspected;
        assert(inspectInstalledOrdinarySdDirectory(path.c_str(),setupPolicy,[](const char*)->uint32_t{return UINT32_MAX;},setupIdentity,&inspected));
        auto* extra=captureInstalledCapabilities();
        assert(extra && versionInInstalledSnapshot(extra,"rtc.clock")==2);
        releaseInstalledCapabilities(extra);
        assert(Storage.remove((path+"/unexpected").c_str()));
    }
    install(39);auto* retry=captureInstalledCapabilities();assert(retry && versionInInstalledSnapshot(retry,"rtc.clock")==2);releaseInstalledCapabilities(retry);
    // Change a non-executable declared size after the initial inventory/header
    // read, using raw provider I/O so only the final tree check can catch it.
    inspectorApi=api;inspectorExt=risc_storage_volume_extension(api);
    auto hooks=*inspectorExt;hooks.base.struct_size=sizeof(hooks);
    hooks.file_open=inspectorOpen;hooks.base.file_read=inspectorRead;hooks.base.file_close=inspectorClose;
    assert(Storage.bindVolume(&hooks.base));
    OrdinaryPackagePlan inspected;
    OrdinaryInspectionDiagnostic diagnostic;
    changeFinalSize=true;
    assert(!inspectInstalledOrdinarySdDirectory(packagePath(39).c_str(),setupPolicy,[](const char*)->uint32_t{return UINT32_MAX;},setupIdentity,&inspected,&diagnostic));
    assert(!std::strcmp(diagnostic.stage,"final-tree") && !std::strcmp(diagnostic.entry,"privileged-imports.v1"));
    assert(!changeFinalSize);install(39);
    failManifestClose=true;manifestCloseAttempts=0;
    assert(!inspectInstalledOrdinarySdDirectory(packagePath(39).c_str(),setupPolicy,[](const char*)->uint32_t{return UINT32_MAX;},setupIdentity,&inspected,&diagnostic));
    assert(!std::strcmp(diagnostic.stage,"manifest-read") && !std::strcmp(diagnostic.entry,".package.json"));
    assert(manifestCloseAttempts==2 && !Storage.generation().quiescent && !Storage.begin());
    assert(driver->quiesce());driver->stop();std::free(card_image);
    std::printf("Actual SD/FatFs/HAL/inspector/resolver: 40 providers %u sectors/%llu modeled ms; warm zero I/O, deadline, malformed/header/size/tree failures and retry PASS\n",sectors,(unsigned long long)elapsed);
}
