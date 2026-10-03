/* T5 SD peripheral ELF. SD commands, CRC, busy/error policy and FatFs all live
 * here. Physical SPI is borrowed exclusively from the installed spi.bus ELF.
 * Single-block operations deliberately avoid an uncertain multi-block retry.
 */
#include "RiscPlatformClockV1.h"
#include "RiscSpiBusV1.h"
#include "RiscStorageVolumeV1.h"
#include "../storage_fatfs/sd_protocol.h"
#include "../x4pro_i2c/os_cpu_v1.h"
#include <string.h>

static const risc_platform_clock_api_v1 *clock_api;
static const risc_spi_bus_api_v1 *bus;
static uint64_t bus_claim, bus_session, last_cooperate;
static unsigned cooperate_bytes;
static bool started, mounted, card_ready, io_failed, high_capacity;
static char error[80];
static bool mount_filesystem(void);
static bool equal(const char*a,const char*b) { return a && b && !strcmp(a,b); }
static void fail(const char*text) {
    size_t i=0;
    while(text[i] && i+1<sizeof(error)) { error[i]=text[i];++i; }
    error[i]=0;
}
static void cooperate(unsigned bytes) {
    cooperate_bytes+=bytes;
    if(cooperate_bytes>=4096 || clock_api->monotonic_ms(clock_api->context)-last_cooperate>=4) {
        clock_api->sleep_ms(clock_api->context,1);
        last_cooperate=clock_api->monotonic_ms(clock_api->context);cooperate_bytes=0;
    }
}
static bool begin_spi(uint32_t hz,bool selected) {
    return !bus_session && bus->begin(bus->context,bus_claim,hz,selected,&bus_session);
}
static bool exchange(const uint8_t*tx,uint8_t*rx,size_t size) {
    return bus_session && bus->transfer(bus->context,bus_session,tx,rx,size);
}
static bool end_spi(bool good) {
    if(!bus_session) return false;
    // SD releases MISO after clocks with CS inactive. A failed transfer may
    // have exhausted its budget; end still deasserts CS and drains ownership.
    if(!bus->select(bus->context,bus_session,false) || !exchange(NULL,NULL,1)) good=false;
    if(!bus->end(bus->context,bus_session)) { fail("SPI session cleanup retained");return false; }
    bus_session=0;
    return good;
}
static bool wait_byte(uint8_t desired,uint32_t budget_ms) {
    const uint64_t began=clock_api->monotonic_ms(clock_api->context);
    for(unsigned i=0;i<49152;++i) {
        uint8_t value;
        if(!exchange(NULL,&value,1)) return false;
        if(value==desired) return true;
        if(desired==0xfe && value!=0xff) return false; // card read-error token
        if((i&63u)==63u) clock_api->sleep_ms(clock_api->context,1);
        if(clock_api->monotonic_ms(clock_api->context)-began>=budget_ms) return false;
    }
    return false;
}
static bool command(uint8_t index,uint32_t argument,uint8_t *response) {
    uint8_t frame[6];risc_sd_command(index,argument,frame);
    if(!exchange(NULL,NULL,1) || !exchange(frame,NULL,sizeof(frame))) return false;
    for(unsigned i=0;i<16;++i) {
        if(!exchange(NULL,response,1)) return false;
        if(!(*response&0x80)) return true;
    }
    return false;
}
static bool simple_command(uint8_t index,uint32_t arg,uint8_t *r1,uint8_t *extra,size_t size) {
    if(!begin_spi(400000,true)) return false;
    bool good=command(index,arg,r1);
    if(good && size) good=exchange(NULL,extra,size);
    return end_spi(good);
}
static bool init_card(void) {
    uint8_t r1=0xff,extra[4];bool version2=false,ready_card=false;
    high_capacity=false;
    if(bus_session || !begin_spi(400000,false)) { fail("SPI bus unavailable");return false; }
    bool good=exchange(NULL,NULL,10);
    if(!end_spi(good)) return false;
    const uint64_t began=clock_api->monotonic_ms(clock_api->context);
    for(unsigned i=0;i<20;++i) {
        if(simple_command(0,0,&r1,NULL,0) && r1==1) { ready_card=true;break; }
        clock_api->sleep_ms(clock_api->context,10);
    }
    if(!ready_card) { fail("CMD0 idle failed");return false; }
    if(!begin_spi(400000,true)) return false;
    good=command(8,0x1aa,&r1);
    if(good && r1==1) {
        good=exchange(NULL,extra,4) && extra[2]==1 && extra[3]==0xaa;version2=good;
    } else if(good) good=r1==5; // SD v1: illegal CMD8 while idle
    if(!end_spi(good)) { fail("CMD8 failed");return false; }
    ready_card=false;
    for(unsigned i=0;i<200 && clock_api->monotonic_ms(clock_api->context)-began<3000;++i) {
        if(!simple_command(55,0,&r1,NULL,0) || r1>1 ||
           !simple_command(41,version2?0x40000000u:0,&r1,NULL,0)) break;
        if(r1==0) { ready_card=true;break; }
        if(r1!=1) break;
        clock_api->sleep_ms(clock_api->context,10);
    }
    if(!ready_card) { fail("ACMD41 initialization failed");return false; }
    if(version2) {
        if(!simple_command(58,0,&r1,extra,4) || r1 || !(extra[0]&0x80)) { fail("CMD58 failed");return false; }
        high_capacity=(extra[0]&0x40)!=0;
    }
    if(!high_capacity && (!simple_command(16,512,&r1,NULL,0) || r1)) { fail("CMD16 failed");return false; }
    if(!simple_command(59,1,&r1,NULL,0) || r1) { fail("SD CRC enable failed");return false; }
    card_ready=true;return mount_filesystem();
}
static bool read_sector(uint32_t lba,uint8_t out[512]) {
    uint8_t r1,crc[2];
    if(!out || (!high_capacity && lba>UINT32_MAX/512) || !begin_spi(25000000,true)) return false;
    bool good=command(17,high_capacity?lba:lba*512,&r1) && !r1 && wait_byte(0xfe,500) &&
              exchange(NULL,out,512) && exchange(NULL,crc,2) &&
              risc_sd_crc16(out,512)==(uint16_t)(((uint16_t)crc[0]<<8)|crc[1]);
    good=end_spi(good);cooperate(512);return good;
}
static bool write_sector(uint32_t lba,const uint8_t data[512]) {
    uint8_t r1,response,status,token=0xfe;
    if(!data || (!high_capacity && lba>UINT32_MAX/512) || !begin_spi(25000000,true)) return false;
    const uint16_t crc=risc_sd_crc16(data,512);
    const uint8_t checksum[2]={(uint8_t)(crc>>8),(uint8_t)crc};
    bool good=command(24,high_capacity?lba:lba*512,&r1) && !r1 && exchange(NULL,NULL,1) &&
              exchange(&token,NULL,1) && exchange(data,NULL,512) && exchange(checksum,NULL,2) &&
              exchange(NULL,&response,1) && (response&0x1f)==5 && wait_byte(0xff,750) &&
              command(13,0,&r1) && !r1 && exchange(NULL,&status,1) && !status;
    good=end_spi(good);cooperate(512);return good;
}
static bool sync_card(void) {
    if(!begin_spi(25000000,true)) return false;
    return end_spi(wait_byte(0xff,750));
}
static bool transport_idle(void) { return !bus_session; }
#define STORAGE_VOLUME_OS_CPU_MUTEX
#define STORAGE_VOLUME_LABEL "T5S3"
#include "../storage_fatfs/volume.c"

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    /* ModuleV2 serializes initial start/restart before publishing consumers.
     * The opaque kernel mutex lives outside this ELF's PSRAM-backed data. */
    if (!valid_task() || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return false;
    if (!operation_mutex) {
        operation_mutex = xQueueCreateMutex(1); /* queueQUEUE_TYPE_MUTEX */
        if (!operation_mutex) return false; /* No SPI claim before admission. */
    }
    if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&quiesced, false, __ATOMIC_RELEASE);
        __atomic_store_n(&quiescing, false, __ATOMIC_RELEASE);
    }
    if (!enter_lifecycle()) return false;
    bool okay = false;
    if (started || bus_claim || bus_session || has_handles() || !deps) goto done;
    bus = NULL; clock_api = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (deps[i].api_version != 1) continue;
        if (equal(deps[i].capability_id, "spi.bus")) bus = deps[i].api;
        if (equal(deps[i].capability_id, "platform.clock")) clock_api = deps[i].api;
    }
    if (!bus || bus->api_version != 1 || bus->struct_size < sizeof(*bus) ||
        !bus->claim_device || !bus->begin || !bus->select || !bus->transfer || !bus->end || !bus->release_device ||
        !clock_api || clock_api->api_version != 1 || clock_api->struct_size < sizeof(*clock_api) ||
        !clock_api->monotonic_ms || !clock_api->sleep_ms) { clock_api = NULL; bus = NULL; goto done; }
    if (!bus->claim_device(bus->context, 12, &bus_claim)) goto done;
    started = true;
    mounted = card_ready = io_failed = power_down_prepared = false;
    error[0] = 0;
    operation_start = clock_api->monotonic_ms(clock_api->context);
    (void)init_card(); /* Absent card retains the existing refresh capability. */
    okay = true;
done:
    return leave() && okay;
}
static bool quiesce(void) {
    if (!valid_task() || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return false;
    if (!operation_mutex || __atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) return true;
    if (!enter_lifecycle()) return false;
    bool safe = !has_handles() && !bus_session;
    if (safe && bus_claim) {
        safe = bus->release_device(bus->context, bus_claim);
        if (safe) bus_claim = 0;
        /* Failed release retains the exact claim and dependency for retry. */
    }
    if (safe) {
        (void)f_mount(NULL, "", 0);
        started = mounted = card_ready = power_down_prepared = false;
        bus = NULL; clock_api = NULL;
        /* Close admission before the final give. Publish accepted quiescence
         * only after it succeeds, so stop cannot delete a still-owned mutex. */
        __atomic_store_n(&quiescing, true, __ATOMIC_RELEASE);
    }
    if (!leave() || !safe) return false;
    __atomic_store_n(&quiesced, true, __ATOMIC_RELEASE);
    return true;
}
static void stop(void) {
    if (!valid_task() || !operation_mutex || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return;
    if (!__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE) && !quiesce()) return;
    /* Accepted quiescence has no second fallible take/give before unmapping. */
    x4_cpu_mutex retired = operation_mutex;
    operation_mutex = NULL;
    vQueueDelete(retired);
}
static const risc_driver_v2 driver={RISC_PROVIDER_DRIVER_ABI_V2,sizeof(driver),"t5s3-sd",
                                  "storage.volume",1,&api,start,stop,quiesce};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi==2?&driver:NULL; }
