#include <HalStorage.h>
#include <RiscProviderV2.h>
#include <RiscPlatformClockV1.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef TEST_APP_PARSER
#include "native/AppManifest.h"
#endif
extern "C" {
uint8_t *card_image;
uint32_t card_sectors = 131072;
bool card_bad_crc, card_reject_write, card_busy_forever, card_bad_pin;
unsigned card_reads, card_writes;
uint64_t card_time;
const risc_driver_v2 *t5_driver_get(uint32_t);
}
static uint64_t now(void*) { return card_time; }
static void sleep(void*, uint32_t ms) { card_time += ms; }
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
int main(int argc, char **argv) {
    card_image=static_cast<uint8_t*>(std::calloc(card_sectors,512));assert(card_image);
    format(argc>1 && std::strcmp(argv[1],"mbr")==0);
    const auto *driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    const auto *api=static_cast<const risc_storage_volume_api_v1*>(driver->capability);
    const auto *ext=risc_storage_volume_extension(api);assert(ext);
    auto old=*api;old.struct_size=sizeof(old);assert(!risc_storage_volume_extension(&old));
    risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,now,sleep};
    risc_provider_dependency_v1 dep={"platform.clock",1,&clock};
    std::fprintf(stderr,"mount\n");
    // Invalid BPB fails closed without automatic formatting or writes.
    const size_t boot=(argc>1 && std::strcmp(argv[1],"mbr")==0 ? 2048u : 0u)*512u;
    card_image[boot+510]=0;
    assert(driver->start(&dep,1)); assert(!api->ready(nullptr)); assert(!card_writes);
    assert(driver->quiesce()); card_image[boot+510]=0x55;
    assert(driver->start(&dep,1));
    char error[80];api->last_error(nullptr,error,sizeof(error));if(!api->ready(nullptr))std::fprintf(stderr,"mount: %s\n",error);
    assert(api->ready(nullptr));assert(Storage.bindVolume(api));
    std::fprintf(stderr,"mkdir\n");
    assert(Storage.mkdir("/Apps/springboard/.staging/deep/package/path"));
    assert(Storage.mkdir("/Drivers/very-long-provider-name"));
    assert(Storage.mkdir("/.crosspoint"));
    assert(!Storage.mkdir("/Apps/../escape"));
    assert(!Storage.open("/Apps/../escape",O_WRONLY|O_CREAT));
    std::fprintf(stderr,"settings\n");
    assert(Storage.writeFile("/.crosspoint/settings.json",String("{\"language\":0}")));
    assert(Storage.readFile("/.crosspoint/settings.json")=="{\"language\":0}");
    std::fprintf(stderr,"large file\n");
    std::vector<uint8_t> block(4096);for(size_t i=0;i<block.size();++i)block[i]=i;
    auto file=Storage.open("/Apps/springboard/springboard.elf",O_RDWR|O_CREAT|O_EXCL);assert(file);
    const auto writing=Storage.generation();assert(!writing.quiescent);
    for(unsigned i=0;i<64;++i)assert(file.write(block.data(),block.size())==block.size());
    file.flush();assert(!file.getError());assert(file.fileSize64()==262144);
    assert(file.seek64(180003));uint8_t bytes[33];assert(file.read(bytes,sizeof(bytes))==sizeof(bytes));assert(bytes[0]==(180003%256));
    assert(file.close());const auto closed=Storage.generation();assert(closed.quiescent);
    assert(Storage.writeFile("/Apps/springboard/springboard.json",String(R"({"file_name":"springboard.elf","display_name":"Apps","version":"1.0.0","min_firmware_version":"1.3.0","icon":"solid:f00a"})")));
#ifdef TEST_APP_PARSER
    t5_app_manifest_t parsed{};
    assert(readAppManifest("/Apps/springboard/springboard.json", parsed));
    assert(parsed.compatible && std::strcmp(parsed.file_name, "springboard.elf")==0);
#endif
    std::fprintf(stderr,"inventory\n");
    auto inventory=Storage.open("/Apps");assert(inventory.isDirectory());
    auto package=inventory.openNextFile();assert(package.isDirectory());char name[128];package.getName(name,sizeof(name));assert(std::strcmp(name,"springboard")==0);
    auto reader=Storage.open("/Apps/springboard/springboard.elf");assert(reader);
    auto manifest=Storage.open("/Apps/springboard/springboard.json");assert(manifest);
    assert(reader.seek64(250000));assert(reader.read(bytes,sizeof(bytes))==sizeof(bytes));
    assert(!Storage.remove("/Apps/springboard/springboard.elf"));
    assert(!Storage.rename("/Apps/springboard","/Apps/replaced"));
    assert(!api->refresh(nullptr));assert(!driver->quiesce());
    assert(reader.close()&&manifest.close()&&package.close()&&inventory.close());
    // Case/SFN aliases must not circumvent descendant ownership.
    auto alias = Storage.open("/APPS/SPRING~1/springboard.elf"); assert(alias);
    assert(!Storage.rename("/Apps/springboard", "/Apps/aliased")); assert(alias.close());
    assert(!Storage.rename("/Apps/springboard", "/Apps/springboard/.staging/loop"));
    assert(!Storage.rename("/Apps/springboard", "/APPS/SPRING~1"));
    assert(!Storage.mkdir("/must-not-exist/../escape")); assert(!Storage.exists("/must-not-exist"));
    // Open mode semantics and bounded independently allocated handles.
    assert(Storage.writeFile("/modes", String("abc")));
    auto append = Storage.open("/modes", O_WRONLY | O_APPEND | O_SYNC); assert(append);
    assert(append.seek64(0)); assert(append.write("d",1)==1); assert(append.close());
    assert(Storage.readFile("/modes")=="abcd");
    auto trunc = Storage.open("/modes", O_WRONLY | O_TRUNC); assert(trunc); assert(trunc.fileSize64()==0); assert(trunc.close());
    uint32_t slots[12];
    for (auto &h: slots) { h=ext->file_open(nullptr,"/modes",RISC_STORAGE_OPEN_READ); assert(h); }
    assert(!ext->file_open(nullptr,"/modes",RISC_STORAGE_OPEN_READ));
    for (auto h:slots) assert(api->file_close(nullptr,h,true));
    std::fprintf(stderr,"transaction\n");
    // Ordinary transaction shape: staged generation, destination refuses
    // overwrite, old generation moved to backup, stage committed by rename.
    assert(Storage.mkdir("/Apps/.stage-new"));assert(Storage.writeFile("/Apps/.stage-new/manifest.json",String("new")));
    assert(!Storage.rename("/Apps/.stage-new","/Apps/springboard"));
    auto parentInventory=Storage.open("/Apps"); assert(parentInventory);
    assert(Storage.rename("/Apps/springboard","/Apps/.backup-old"));
    assert(Storage.rename("/Apps/.stage-new","/Apps/springboard"));
    assert(parentInventory.close());
    assert(Storage.readFile("/Apps/springboard/manifest.json")=="new");
    assert(Storage.exists("/Apps/.backup-old/springboard.elf"));
    assert(Storage.writeFile("/Drivers/very-long-provider-name/driver.elf",String("provider")));
    assert(Storage.writeFile("/Drivers/very-long-provider-name/manifest.json",String("{}")));
    assert(Storage.writeFile("/.crosspoint/caf\xc3\xa9.json",String("utf8")));
    assert(Storage.readFile("/.crosspoint/caf\xc3\xa9.json")=="utf8");
    std::fprintf(stderr,"remove\n");
    assert(Storage.removeDir("/Apps/.backup-old"));
    uint64_t length=0;auto stale=api->file_open_read(nullptr,"/.crosspoint/settings.json",&length);assert(stale);
    assert(api->file_close(nullptr,stale,true));auto fresh=api->file_open_read(nullptr,"/.crosspoint/settings.json",&length);assert(fresh&&fresh!=stale);
    assert(!api->file_read(nullptr,stale,bytes,sizeof(bytes)));assert(api->file_close(nullptr,fresh,true));
    auto abort=api->file_open_write(nullptr,"/abort-me.json");assert(abort);assert(api->file_write(nullptr,abort,"x",1)==1);assert(api->file_close(nullptr,abort,false));assert(!Storage.exists("/abort-me.json"));
    std::fprintf(stderr,"remount\n");
    assert(Storage.begin());assert(Storage.readFile("/Apps/springboard/manifest.json")=="new");
    // Malformed cyclic root traversal must fail, not hang or report EOF.
    const size_t root=boot+(32u+2048u)*512u, fat=boot+32u*512u;
    std::vector<uint8_t> savedRoot(card_image+root,card_image+root+512);
    std::vector<uint8_t> savedFat(card_image+fat,card_image+fat+512);
    for (unsigned i=0;i<512;i+=32) card_image[root+i]=0xe5;
    put32(card_image+fat+8,2); assert(Storage.begin());
    auto cyclic=Storage.open("/"); assert(cyclic);
    assert(!cyclic.openNextFile()); assert(cyclic.getError()); assert(!Storage.ready()); assert(cyclic.close());
    std::memcpy(card_image+root,savedRoot.data(),512); std::memcpy(card_image+fat,savedFat.data(),512);
    assert(Storage.begin());
    std::fprintf(stderr,"crc failure\n");
    // CRC failure is not EOF, stale handles cannot become live after remount.
    auto errorDirectory=Storage.open("/Drivers"); assert(errorDirectory);
    auto fault=Storage.open("/.crosspoint/settings.json");assert(fault);card_bad_crc=true;
    assert(fault.read(bytes,sizeof(bytes))<0);assert(fault.getError());assert(!Storage.ready());
    assert(!errorDirectory.openNextFile()); assert(errorDirectory.getError()); assert(errorDirectory.close());
    assert(fault.close());card_bad_crc=false;
    assert(Storage.begin());
    std::fprintf(stderr,"write failure\n");
    // Uncertain write retains ownership and forbids remount or unload.
    auto writer=Storage.open("/write-failure.bin",O_WRONLY|O_CREAT);assert(writer);
    if (argc>2) card_busy_forever=true; else card_reject_write=true;
    const auto began=card_time;
    assert(writer.write(block.data(),block.size())<block.size());assert(writer.getError()); assert(card_time-began<16000);
    assert(!writer.close());assert(!Storage.begin());assert(!driver->quiesce());
    assert(!card_bad_pin);assert(card_reads&&card_writes);
    std::puts("production FatFs + native SD wire + HalStorage: PASS");
    // Deliberately retain the failed writer until process exit, like reboot.
    return 0;
}
