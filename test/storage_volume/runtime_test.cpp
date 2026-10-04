#include <HalStorage.h>
#include <RiscProviderV2.h>
#include <RiscPlatformClockV1.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "os_cpu_fake.h"
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
static void sleep(void*, uint32_t ms) { ++sleeps; card_time += ((ms + 9u) / 10u) * 10u; }
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
#include "directory_iteration_test.inc"
#ifndef TEST_SPI_TRANSPORT
#include "bootstrap_handoff.inc"
#endif
int main(int argc, char **argv) {
    card_image=static_cast<uint8_t*>(std::calloc(card_sectors,512));assert(card_image);
    format(argc>1 && std::strcmp(argv[1],"mbr")==0);
    const auto *driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    const auto *api=static_cast<const risc_storage_volume_api_v1*>(driver->capability);
    const auto *ext=risc_storage_volume_extension(api);assert(ext);
    const auto *power=risc_storage_volume_power(api);assert(power);
    auto old=*api;old.struct_size=sizeof(old);assert(!risc_storage_volume_extension(&old));
    risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,now,sleep};
    risc_provider_dependency_v1 deps[]={{"platform.clock",1,&clock}
#ifdef TEST_SPI_TRANSPORT
        ,{"spi.bus",1,&SpiCardFixture::api}
#endif
    };
#ifndef TEST_SPI_TRANSPORT
    if (argc > 1 && std::strcmp(argv[1], "bootstrap-hold") == 0) {
        // Execute the current production SdBootReader cleanup, not a second
        // list of assumed pin writes. Its checked deinit leaves power held off.
        assert(BootstrapHandoff::unmount() == BootstrapHandoff::ESP_OK);
        assert(!BootstrapHandoff::claimed.held && card_sleep_off && card_power_off);
        assert(driver->start(deps, sizeof(deps) / sizeof(deps[0])));
        char why[80]{}; api->last_error(nullptr, why, sizeof(why));
        std::fprintf(stderr, "bootstrap handoff: hold=%d power_off=%d mounted=%d reason=%s sectors=%u\n",
                     card_sleep_off, card_power_off, api->ready(nullptr), why, card_reads);
        assert(!card_sleep_off && !card_power_off && api->ready(nullptr) && card_reads &&
               "bootstrap SD release required");
        assert(Storage.bindVolume(api));
        assert(Storage.mkdir("/Apps") && Storage.writeFile("/Apps/probe.txt", String("retained content")));
        assert(Storage.readFile("/Apps/probe.txt") == "retained content");
        assert(driver->quiesce()); driver->stop();
        assert(sd_mutex_creates == sd_mutex_deletes && !card_bad_pin);
        std::free(card_image);
        std::puts("actual bootstrap cleanup to SD provider mount/read/write PASS");
        return 0;
    }
#endif
    auto invalidClock=clock;invalidClock.monotonic_ms=nullptr;
    deps[0].api=&invalidClock;
    assert(!driver->start(deps,sizeof(deps)/sizeof(deps[0])));
    assert(driver->quiesce()); // Failed dependency validation must be safe to unwind.
    deps[0].api=&clock;
    std::fprintf(stderr,"mount\n");
    // Invalid BPB fails closed without automatic formatting or writes.
    const size_t boot=(argc>1 && std::strcmp(argv[1],"mbr")==0 ? 2048u : 0u)*512u;
    card_image[boot+510]=0;
    assert(driver->start(deps,sizeof(deps)/sizeof(deps[0]))); assert(!api->ready(nullptr)); assert(!card_writes);
    assert(driver->quiesce()); card_image[boot+510]=0x55;
    assert(driver->start(deps,sizeof(deps)/sizeof(deps[0])));
    char error[80];api->last_error(nullptr,error,sizeof(error));if(!api->ready(nullptr))std::fprintf(stderr,"mount: %s\n",error);
    assert(api->ready(nullptr));assert(Storage.bindVolume(api));
    if (argc>1 && std::strncmp(argv[1],"directory-",10)==0) {
        directoryIterationTests(argv[1],driver,api); return 0;
    }
    std::fprintf(stderr,"mkdir\n");
    assert(Storage.mkdir("/Apps/springboard/.staging/deep/package/path"));
    assert(Storage.mkdir("/Drivers/very-long-provider-name"));
    assert(Storage.mkdir("/.crosspoint"));
    assert(!Storage.mkdir("/Apps/../escape"));
    assert(!Storage.open("/Apps/../escape",O_WRONLY|O_CREAT));
#ifndef TEST_SPI_TRANSPORT
    if (argc > 1 && std::strcmp(argv[1], "clock-registers") == 0) {
        assert(sd_clock_mux_writes > 80 && sd_clock_direction_writes > 80); // original identification edges retained
        const auto mux = sd_clock_mux_writes, direction = sd_clock_direction_writes, levels = sd_clock_levels;
        sd_clock_cycles = UINT32_MAX - 15u;
        sd_clock_check_phases = true;
        const std::string bytes(4096, 'Q');
        assert(Storage.writeFile("/clock-probe", bytes.c_str()));
        assert(Storage.readFile("/clock-probe") == bytes.c_str());
        assert(sd_clock_levels > levels + 4096 * 8 * 2);
        assert(sd_clock_mux_writes == mux && sd_clock_direction_writes == direction &&
               "selected-card clock edges must not reconfigure CLK");
        assert(sd_clock_wraps && !card_bad_pin && driver->quiesce());
        std::free(card_image);
        std::puts("Production GPIO helpers: identification setup retained, selected-card read/write clock levels only PASS");
        return 0;
    }
#endif
#ifndef TEST_SPI_TRANSPORT
    if (argc > 1 && (!std::strcmp(argv[1], "clock-stuck-counter") || !std::strcmp(argv[1], "clock-stuck-readback"))) {
        const unsigned before = sd_clock_cycle_calls;
        sd_clock_counter_stuck = !std::strcmp(argv[1], "clock-stuck-counter");
        sd_clock_readback_stuck = !sd_clock_counter_stuck;
        auto rejected = Storage.open("/clock-fault", O_WRONLY | O_CREAT);
        assert(!rejected && !Storage.ready());
        assert(sd_clock_cycle_calls - before <= 129); // first failing edge, no unbounded retry
        std::free(card_image);
        std::puts("SD edge guard: stuck clock read-back/counter fails media I/O within poll bound PASS");
        return 0;
    }
#endif
    std::fprintf(stderr,"settings\n");
    assert(Storage.writeFile("/.crosspoint/settings.json",String("{\"language\":0}")));
    assert(Storage.readFile("/.crosspoint/settings.json")=="{\"language\":0}");
    // Failure of the OS ownership transition must reach every success-shaped
    // return type. Keep this generation mapped even when the card I/O succeeded.
    if (argc > 1 && std::strncmp(argv[1], "mutex-", 6) == 0) {
        const char *kind = argv[1] + 6;
        uint64_t size = 0, position = 0; bool directory = false; char byte = 0;
        const auto reader = api->file_open_read(nullptr, "/.crosspoint/settings.json", &size);
        assert(reader);
        const auto writer = api->file_open_write(nullptr, "/give-failure"); assert(writer);
        const auto dir = api->dir_open(nullptr, "/"); assert(dir);
        sd_mutex_fail_give = true;
        if (!std::strcmp(kind, "open-give"))
            assert(!api->file_open_read(nullptr, "/.crosspoint/settings.json", &size));
        else if (!std::strcmp(kind, "read-give")) assert(!api->file_read(nullptr, reader, &byte, 1));
        else if (!std::strcmp(kind, "write-give")) assert(!api->file_write(nullptr, writer, "x", 1));
        else if (!std::strcmp(kind, "close-give")) assert(!api->file_close(nullptr, reader, true));
        else if (!std::strcmp(kind, "dir-give")) assert(!ext->dir_close_checked(nullptr, dir));
        else if (!std::strcmp(kind, "stat-give")) assert(!api->stat(nullptr, "/", &size, &directory));
        else if (!std::strcmp(kind, "error-give")) assert(ext->handle_error(nullptr, reader, false) != 0);
        else if (!std::strcmp(kind, "info-give")) assert(!ext->file_info(nullptr, reader, &size, &position));
        else assert(!"unknown give failure scenario");
        sd_mutex_fail_give = false;
        const unsigned reads = card_reads, writes = card_writes, deletes = sd_mutex_deletes;
        assert(!api->ready(nullptr) && !api->refresh(nullptr));
        assert(!power->prepare_power_down(nullptr) && !power->cancel_power_down(nullptr));
        const auto *commit = risc_storage_volume_power_commit(api);
        assert(!commit || !commit->commit_power_down(nullptr));
        assert(!api->file_close(nullptr, reader, true) && !driver->quiesce());
        driver->stop();
        assert(card_reads == reads && card_writes == writes && sd_mutex_deletes == deletes);
        assert(!card_sleep_off && !card_bad_pin);
        std::free(card_image); std::printf("production SD actual FatFs poison retention: %s PASS\n", kind);
        return 0;
    }
    // Retain read metadata across a cancelled sleep; never unload a live handle.
    uint64_t sleepSize=0;
    auto sleepHandle=api->file_open_read(nullptr,"/.crosspoint/settings.json",&sleepSize);
    assert(sleepHandle);
    assert(power->prepare_power_down(nullptr));assert(power->prepare_power_down(nullptr));
    assert(!api->ready(nullptr));assert(!api->refresh(nullptr));assert(!driver->quiesce());
    char sleepByte=0;assert(!api->file_read(nullptr,sleepHandle,&sleepByte,1));
    assert(!api->file_close(nullptr,sleepHandle,true));
    assert(power->cancel_power_down(nullptr));assert(api->ready(nullptr));
    assert(api->file_read(nullptr,sleepHandle,&sleepByte,1)==1 && sleepByte=='{');
    assert(api->file_close(nullptr,sleepHandle,true));
    if (argc > 1 && std::strcmp(argv[1], "sleep") == 0) {
        const auto *commit = risc_storage_volume_power_commit(api);
#ifdef TEST_SPI_TRANSPORT
        // This board has no terminal rail callback; only its reversible freeze
        // is exported, so generic callers cannot assume an X4 sleep commit.
        assert(!commit && !card_sleep_off && !card_sleep_commits);
        sleepHandle = api->file_open_read(nullptr, "/.crosspoint/settings.json", &sleepSize);
        assert(sleepHandle && power->prepare_power_down(nullptr));
        assert(!api->ready(nullptr) && !driver->quiesce());
        assert(!api->file_read(nullptr, sleepHandle, &sleepByte, 1));
        assert(!api->file_close(nullptr, sleepHandle, true));
        assert(power->cancel_power_down(nullptr) && api->ready(nullptr));
        assert(api->file_read(nullptr, sleepHandle, &sleepByte, 1) == 1 && sleepByte == '{');
        assert(api->file_close(nullptr, sleepHandle, true));
        assert(driver->quiesce()); driver->stop();
        assert(sd_mutex_creates == sd_mutex_deletes && !SpiCardFixture::claimed && !SpiCardFixture::active);
        assert(driver->start(deps, sizeof(deps) / sizeof(deps[0])) && api->ready(nullptr));
        assert(driver->quiesce()); driver->stop();
        assert(sd_mutex_creates == sd_mutex_deletes && !card_bad_pin);
        std::free(card_image);
        std::puts("T5 SD reversible sleep retains readers and restarts after cleanup PASS");
#else
        assert(commit);
        assert(!commit->commit_power_down(nullptr) && !card_sleep_off);
        sleepHandle = api->file_open_read(nullptr, "/.crosspoint/settings.json", &sleepSize);
        assert(sleepHandle && power->prepare_power_down(nullptr));
        assert(commit->commit_power_down(nullptr) && card_sleep_off && card_sleep_commits == 1);
        assert(commit->commit_power_down(nullptr) && card_sleep_commits == 1);
        assert(!power->cancel_power_down(nullptr));
        assert(!api->ready(nullptr) && !driver->quiesce());
        assert(!api->file_read(nullptr, sleepHandle, &sleepByte, 1));
        assert(!card_bad_pin);
        std::free(card_image);
        std::puts("X4 SD terminal sleep commit retains frozen readers and powers off once PASS");
#endif
        return 0;
    }
    std::fprintf(stderr,"large file\n");
    std::vector<uint8_t> block(4096);for(size_t i=0;i<block.size();++i)block[i]=i;
    auto file=Storage.open("/Apps/springboard/springboard.elf",O_RDWR|O_CREAT|O_EXCL);assert(file);
    const auto writing=Storage.generation();assert(!writing.quiescent);
    assert(!power->prepare_power_down(nullptr));assert(api->ready(nullptr));
    for(unsigned i=0;i<64;++i)assert(file.write(block.data(),block.size())==block.size());
    file.flush();assert(!file.getError());assert(file.fileSize64()==262144);
    assert(file.seek64(180003));uint8_t bytes[33];assert(file.read(bytes,sizeof(bytes))==sizeof(bytes));assert(bytes[0]==(180003%256));
    assert(file.close());const auto closed=Storage.generation();assert(closed.quiescent);
    // Deterministic scheduler-cost profile of the real provider/HAL path.
    // Model a 10 ms OS tick. The former per-64-byte sleeps exceed 4600
    // waits for this 256 KiB read alone; batching must retain cooperation.
    auto throughput = Storage.open("/Apps/springboard/springboard.elf"); assert(throughput);
    const unsigned beforeSleeps = sleeps, beforeReads = card_reads;
    const uint64_t beforeTime = card_time;
    for (unsigned i=0; i<64; ++i) {
        assert(throughput.read(block.data(),block.size()) == (int)block.size());
        for(size_t j=0;j<block.size();++j) assert(block[j] == (uint8_t)j);
    }
    assert(throughput.close());
    const unsigned waits = sleeps-beforeSleeps;
    std::printf("PROFILE 256KiB: sectors=%u waits=%u modeled_ms=%llu\n",
                card_reads-beforeReads, waits, (unsigned long long)(card_time-beforeTime));
    assert(waits > 0 && waits < 512); // bounded real yields, no tiny-read sleep tax

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
    assert(!power->prepare_power_down(nullptr));assert(!power->cancel_power_down(nullptr));
    assert(!card_bad_pin);assert(card_reads&&card_writes);
    std::puts("production FatFs + native SD wire + HalStorage: PASS");
    // Deliberately retain the failed writer until process exit, like reboot.
    return 0;
}
