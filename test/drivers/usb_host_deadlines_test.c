/* Production host ELF, with fault injection only at the controller boundary.
 * Run retained scenarios in separate processes: unsafe mappings are not freed. */
#include "RiscUsbHostDeadlinesV1.h"
#include "RiscUsbControllerDeadlinesV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const uint8_t config[] = {
    9,2,41,0,2,1,0,0x80,50,
    9,4,0,0,0,2,2,1,0,
    9,4,1,0,2,10,0,0,0,
    7,5,0x81,2,64,0,0, 7,5,0x02,2,64,0,0
};
static uint64_t ticks, physical_serial = 100;
static unsigned legacy_calls, event_calls, config_calls, claim_calls, release_calls;
static unsigned control_calls, read_calls, write_calls, quiesce_calls;
static uint32_t event_step, config_step, claim_step, release_step, io_step;
static uint32_t event_budget[64], config_budget, claim_budget, release_budget, io_budget;
static int32_t event_error, config_error, claim_error, release_error, io_error;
static bool partial_claim, missing_claim, malformed_config, duplicate_claim;
static bool busy_physical[2];
static uint64_t physical_claims[2];
static risc_usb_controller_event_v1 events[40];
static size_t event_head, event_tail;
static const risc_driver_v2 *driver;
static const risc_usb_host_deadlines_v1 *host;
static const risc_usb_host_deadline_api_v1 *timed;
static const risc_usb_host_discovery_v1 *base;
static void add(uint32_t kind, uint64_t id) {
    assert(event_tail < 40);
    events[event_tail++] = (risc_usb_controller_event_v1){kind,id};
}
static uint64_t clock_ms(void *ctx) { (void)ctx; return ticks; }
static int32_t event(void *ctx, risc_usb_controller_event_v1 *out, uint32_t budget) {
    (void)ctx;
    assert(budget && budget <= 1000 && event_calls < 64);
    event_budget[event_calls++] = budget; ticks += event_step;
    if (event_error) return event_error;
    if (event_head == event_tail) return 0;
    *out = events[event_head++]; return 1;
}
static int32_t configuration(void *ctx, uint64_t device, uint8_t *out,
                             size_t *length, uint16_t *vid, uint16_t *pid,
                             uint32_t budget) {
    (void)ctx; assert(device && budget && out && *length >= sizeof(config));
    ++config_calls; config_budget = budget; ticks += config_step;
    if (config_error) return config_error;
    memcpy(out, config, sizeof(config)); *length = sizeof(config);
    if (malformed_config) out[2] = 255;
    *vid = 0x1234; *pid = 0x5678; return RISC_STREAM_OK;
}
static int32_t claim(void *ctx, uint64_t device, uint8_t iface, uint8_t alt,
                     uint64_t *out, uint32_t budget) {
    (void)ctx; assert(device && iface < 2 && !alt && budget && !*out);
    ++claim_calls; claim_budget = budget; ticks += claim_step;
    if (!missing_claim && (!claim_error || partial_claim)) {
        assert(!busy_physical[iface]); busy_physical[iface] = true;
        *out = duplicate_claim ? physical_serial : ++physical_serial; physical_claims[iface] = *out;
    }
    return claim_error;
}
static int32_t release(void *ctx, uint64_t physical, uint32_t budget) {
    (void)ctx; assert(physical > 100 && budget);
    ++release_calls; release_budget = budget; ticks += release_step;
    if (release_error) return release_error;
    for(unsigned i=0;i<2;++i) if(physical_claims[i]==physical) busy_physical[i]=false;
    return RISC_STREAM_OK;
}
static int32_t control(void *ctx, uint64_t device, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *out,
                       uint16_t length, uint32_t budget) {
    (void)ctx; (void)type; (void)request; (void)value; (void)index; (void)out;
    assert(device && budget); ++control_calls; io_budget=budget; ticks+=io_step;
    return io_error ? io_error : length;
}
static int32_t read_data(void *ctx, uint64_t token, uint8_t ep, uint8_t *out,
                         size_t length, uint32_t budget) {
    (void)ctx; assert(token && ep==0x81 && length && budget);
    ++read_calls; io_budget=budget; ticks+=io_step;
    if (io_error) return io_error;
    out[0]=0x5a; return 1;
}
static int32_t write_data(void *ctx, uint64_t token, uint8_t ep, const uint8_t *out,
                          size_t length, uint32_t budget) {
    (void)ctx; assert(token && ep==0x02 && out && length && budget);
    ++write_calls; io_budget=budget; ticks+=io_step;
    return io_error ? io_error : (int32_t)length;
}
static int32_t old_event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx; (void)out; ++legacy_calls; return 0;
}
static bool old_config(void *ctx, uint64_t d, uint8_t *o, size_t *n, uint16_t *v, uint16_t *p) {
    (void)ctx; (void)d; (void)o; (void)n; (void)v; (void)p; ++legacy_calls; return false;
}
static bool old_claim(void *ctx, uint64_t d, uint8_t i, uint8_t a, uint64_t *o) {
    (void)ctx; (void)d; (void)i; (void)a; (void)o; ++legacy_calls; return false;
}
static bool old_release(void *ctx, uint64_t id) {
    (void)ctx; (void)id; ++legacy_calls; return true;
}
static int32_t old_control(void *ctx, uint64_t d, uint8_t t, uint8_t r,
                           uint16_t v, uint16_t i, uint8_t *p, uint16_t n, uint32_t b) {
    (void)ctx; (void)d; (void)t; (void)r; (void)v; (void)i; (void)p; (void)n; (void)b;
    ++legacy_calls; return -1;
}
static int32_t old_read(void *ctx, uint64_t d, uint8_t e, uint8_t *p, size_t n, uint32_t b) {
    (void)ctx; (void)d; (void)e; (void)p; (void)n; (void)b; ++legacy_calls; return -1;
}
static int32_t old_write(void *ctx, uint64_t d, uint8_t e, const uint8_t *p, size_t n, uint32_t b) {
    (void)ctx; (void)d; (void)e; (void)p; (void)n; (void)b; ++legacy_calls; return -1;
}
static bool quiesce(void *ctx) { (void)ctx; ++quiesce_calls; return true; }
static risc_usb_controller_deadline_api_v1 controller_timed = {
    1, sizeof(controller_timed), clock_ms, event, configuration, claim, release,
    control, read_data, write_data
};
static risc_usb_controller_deadlines_v1 controller = {
    {{{1,sizeof(controller),NULL,old_event,old_config,old_claim,old_release,
        old_control,old_read,old_write,quiesce},NULL},NULL},
    RISC_USB_CONTROLLER_DEADLINES_TAG_V1, 1, &controller_timed
};
static uint64_t device(void) {
    uint64_t ids[8]={0}; size_t n=8;
    assert(timed->snapshot(NULL,ids,&n,100)==RISC_STREAM_OK && n==1);
    return ids[0];
}
static uint64_t claimed(uint64_t d, uint8_t i) {
    uint64_t id=0;
    assert(timed->claim(NULL,d,i,0,&id,100)==RISC_STREAM_OK && id);
    return id;
}
static void clean(uint64_t id) { assert(timed->release(NULL,id,100)==RISC_STREAM_OK); }
static void retained(void) {
    unsigned released=release_calls, old=legacy_calls, q=quiesce_calls;
    uint64_t ids[8]={0}; size_t n=8;
    assert(timed->snapshot(NULL,ids,&n,100)==RISC_STREAM_RETAINED);
    assert(timed->release(NULL,1,100)==RISC_STREAM_RETAINED);
    assert(!driver->quiesce()); driver->stop();
    assert(release_calls==released && legacy_calls==old && quiesce_calls==q);
    assert(base->host.struct_size==sizeof(*host));
    puts("Production host sticky retained custody: PASS");
}
int main(int argc, char **argv) {
    assert(argc==3);
    _Static_assert(offsetof(risc_usb_host_deadlines_v1,extension_tag)==sizeof(risc_usb_host_snapshot_v1),"host prefix");
    _Static_assert(offsetof(risc_usb_controller_deadlines_v1,extension_tag)==sizeof(risc_usb_controller_diagnostics_v1),"controller prefix");
    void *elf=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); assert(elf);
    risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(elf,"t5_driver_get"); assert(get);
    driver=get(2); assert(driver); host=driver->capability; base=&host->snapshot.discovery;
    risc_provider_dependency_v1 dep={"usb.controller",1,&controller};
    const char *test=argv[2];
    if (!strcmp(test,"gate")) {
        for (unsigned mode=0;mode<8;++mode) {
            controller.diagnostics.base.controller.struct_size=sizeof(controller);
            controller.extension_tag=RISC_USB_CONTROLLER_DEADLINES_TAG_V1;
            controller.extension_version=1; controller.deadlines=&controller_timed;
            controller_timed.api_version=1; controller_timed.struct_size=sizeof(controller_timed);
            controller_timed.bulk_write=write_data;
            if(mode==0) controller.diagnostics.base.controller.struct_size=sizeof(risc_usb_controller_api_v1);
            if(mode==1) controller.diagnostics.base.controller.struct_size=sizeof(controller)-1;
            if(mode==2) controller.extension_tag=0;
            if(mode==3) controller.extension_version=2;
            if(mode==4) controller.deadlines=NULL;
            if(mode==5) controller_timed.struct_size=sizeof(controller_timed)-1;
            if(mode==6) controller_timed.api_version=2;
            if(mode==7) controller_timed.bulk_write=NULL;
            assert(driver->start(&dep,1));
            assert(base->host.struct_size==sizeof(risc_usb_host_snapshot_v1));
            assert(!host->extension_tag && !host->deadlines && !event_calls);
            size_t n=8; risc_usb_device_identity_v1 ids[8];
            assert(host->snapshot.snapshot(NULL,ids,&n) && !n);
            driver->stop();
        }
        assert(legacy_calls==8); puts("Host deadline admission and complete legacy prefix: PASS"); return 0;
    }
    assert(driver->start(&dep,1)); timed=host->deadlines;
    assert(timed && base->host.struct_size==sizeof(*host) && host->extension_tag==RISC_USB_HOST_DEADLINES_TAG_V1);
    assert(timed->now_ms(NULL)==0);
    if (!strcmp(test,"events")) {
        for(unsigned n=0;n<8;++n) { add(1,n+1); add(2,n+1); }
        add(1,42);
        uint64_t ids[8]={999}; size_t n=8; event_step=1;
        assert(timed->snapshot(NULL,ids,&n,100)==RISC_STREAM_IO);
        assert(event_calls==16 && event_head==16 && ids[0]==999 && n==8);
        for(unsigned i=0;i<16;++i) assert(event_budget[i]==100-i);
        assert(timed->snapshot(NULL,ids,&n,100)==RISC_STREAM_OK && n==1 && ids[0]!=999);
    } else if (!strcmp(test,"inventory_fault")) {
        add(1,2); add(1,2);
        uint64_t ids[8]={999}; size_t n=8;
        assert(timed->snapshot(NULL,ids,&n,100)==RISC_STREAM_IO && ids[0]==999);
        assert(timed->snapshot(NULL,ids,&n,100)==RISC_STREAM_IO);
    } else {
        add(1,42); uint64_t d=device(), id=0;
        uint8_t bytes[64]={0}; size_t n=sizeof(bytes); uint16_t vid=0,pid=0;
        if (!strcmp(test,"valid")) {
            event_step=1; config_step=2; claim_step=3;
            assert(timed->configuration(NULL,d,bytes,&n,&vid,&pid,20)==RISC_STREAM_OK);
            assert(config_budget==19 && n==sizeof(config) && vid==0x1234);
            assert(timed->claim(NULL,d,1,0,&id,20)==RISC_STREAM_OK);
            assert(config_budget==19 && claim_budget==17 && id);
            assert(!base->release_checked(NULL,id)); base->host.release(NULL,id);
            assert(!release_calls && !legacy_calls && !driver->quiesce());
            assert(base->control_claim(NULL,id,0x21,0x20,0,1,bytes,7,10)==7 && io_budget==9);
            assert(base->host.bulk_read(NULL,id,0x81,bytes,sizeof(bytes),10)==1 && bytes[0]==0x5a && io_budget==9);
            assert(base->host.bulk_write(NULL,id,0x02,bytes,5,10)==5 && io_budget==9);
            assert(timed->release(NULL,id,7)==RISC_STREAM_OK && release_budget==7);
            assert(!base->release_checked(NULL,id));
        } else if (!strcmp(test,"partial_setup")) {
            uint64_t first=claimed(d,0); claim_error=RISC_STREAM_CANCELLED;
            assert(timed->claim(NULL,d,1,0,&id,20)==RISC_STREAM_CANCELLED && !id);
            assert(!driver->quiesce() && !quiesce_calls);
            clean(first); assert(release_calls==1);
        } else if (!strcmp(test,"partial_claim")) {
            claim_error=RISC_STREAM_TIMEOUT; partial_claim=true;
            assert(timed->claim(NULL,d,0,0,&id,20)==RISC_STREAM_RETAINED && id);
            assert(!base->release_checked(NULL,id) && !release_calls); retained(); return 0;
        } else if (!strcmp(test,"duplicate_claim")) {
            (void)claimed(d,0); duplicate_claim=true;
            assert(timed->claim(NULL,d,1,0,&id,20)==RISC_STREAM_RETAINED && id);
            retained(); return 0;
        } else if (!strcmp(test,"unknown_claim")) {
            missing_claim=true;
            assert(timed->claim(NULL,d,0,0,&id,20)==RISC_STREAM_RETAINED && !id);
            retained(); return 0;
        } else if (!strcmp(test,"claim_overrun")) {
            claim_step=21;
            assert(timed->claim(NULL,d,0,0,&id,20)==RISC_STREAM_RETAINED && id);
            retained(); return 0;
        } else if (!strcmp(test,"timeout")) {
            event_step=3; config_step=7;
            assert(timed->claim(NULL,d,0,0,&id,10)==RISC_STREAM_TIMEOUT && !id);
            assert(config_budget==7 && !claim_calls && ticks==10);
            event_step=config_step=0; id=claimed(d,0); clean(id);
        } else if (!strcmp(test,"cancel")) {
            event_error=RISC_STREAM_CANCELLED;
            assert(timed->claim(NULL,d,0,0,&id,10)==RISC_STREAM_CANCELLED && !id && !claim_calls && !config_calls);
            event_error=0; config_error=RISC_STREAM_CANCELLED;
            assert(timed->claim(NULL,d,0,0,&id,10)==RISC_STREAM_CANCELLED && !id && !claim_calls);
        } else if (!strcmp(test,"stale")) {
            id=claimed(d,1); add(2,42); add(1,43);
            assert(base->host.bulk_read(NULL,id,0x81,bytes,64,20)==RISC_STREAM_DISCONNECTED && !read_calls);
            uint64_t next=device(); assert(next!=d);
            uint64_t c=0; assert(timed->claim(NULL,d,0,0,&c,20)==RISC_STREAM_DISCONNECTED && !c);
            clean(id); id=claimed(next,1); clean(id);
        } else if (!strcmp(test,"release_failure")) {
            id=claimed(d,1); release_error=RISC_STREAM_TIMEOUT;
            assert(timed->release(NULL,id,7)==RISC_STREAM_RETAINED && release_calls==1 && release_budget==7);
            assert(!base->release_checked(NULL,id)); retained(); return 0;
        } else if (!strcmp(test,"io_retained")) {
            id=claimed(d,1); io_error=RISC_STREAM_RETAINED;
            assert(base->host.bulk_read(NULL,id,0x81,bytes,64,10)==RISC_STREAM_RETAINED);
            retained(); return 0;
        } else if (!strcmp(test,"io_timeout")) {
            id=claimed(d,1); io_error=RISC_STREAM_TIMEOUT; event_step=2; io_step=8;
            assert(base->host.bulk_read(NULL,id,0x81,bytes,64,10)==RISC_STREAM_TIMEOUT && io_budget==8);
            io_step=event_step=0; clean(id);
        } else if (!strcmp(test,"io_overrun")) {
            id=claimed(d,1); io_step=11;
            assert(base->host.bulk_write(NULL,id,0x02,bytes,5,10)==RISC_STREAM_RETAINED);
            retained(); return 0;
        } else if (!strcmp(test,"event_overrun")) {
            event_step=11; uint64_t ids[8]={999}; size_t count=8;
            assert(timed->snapshot(NULL,ids,&count,10)==RISC_STREAM_RETAINED && ids[0]==999);
            retained(); return 0;
        } else if (!strcmp(test,"clock_reversal")) {
            ticks=5; assert(timed->now_ms(NULL)==5); ticks=4;
            assert(timed->now_ms(NULL)==5); retained(); return 0;
        } else if (!strcmp(test,"scope")) {
            id=claimed(d,0); uint64_t second=claimed(d,1);
            assert(base->control_claim(NULL,id,0x21,1,0,1,bytes,1,10)==RISC_STREAM_DENIED);
            assert(base->control_claim(NULL,id,0x40,1,0,0,bytes,1,10)==RISC_STREAM_DENIED);
            assert(base->host.control(NULL,d,0x21,0x0b,0,0,bytes,1,10)==-1);
            assert(!control_calls);
            clean(id); clean(second);
        } else if (!strcmp(test,"invalid")) {
            unsigned old_events=event_calls; uint64_t ids[8]={999}; size_t count=0;
            assert(timed->snapshot(NULL,ids,&count,0)==RISC_STREAM_INVALID && event_calls==old_events);
            count=8; assert(timed->snapshot(NULL,ids,&count,1001)==RISC_STREAM_INVALID && event_calls==old_events);
            count=0; assert(timed->snapshot(NULL,ids,&count,10)==RISC_STREAM_LIMIT && count==1 && ids[0]==999);
            malformed_config=true;
            assert(timed->claim(NULL,d,0,0,&id,20)==RISC_STREAM_IO && !id && !claim_calls);
        } else assert(!"unknown scenario");
    }
    assert(!legacy_calls); assert(driver->quiesce()); driver->stop();
    assert(base->host.struct_size==sizeof(risc_usb_host_snapshot_v1) && !host->deadlines);
    assert(dlclose(elf)==0);
    printf("Production host deadline scenario %s: PASS\n",test);
}
