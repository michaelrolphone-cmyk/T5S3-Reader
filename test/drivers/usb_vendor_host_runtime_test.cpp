// The same deterministic USB host drives the real class and production Runtime
// queues. This is a software custody test, not physical host qualification.
#define main cdc_callback_fixture_main
#define _Static_assert static_assert
#include "usb_vendor_tagged_sessions_test.c"
#undef _Static_assert
#undef main
#include "runtime/streams/ProviderQueueHost.h"
#include "RiscUsbControllerDeadlinesV1.h"

static bool lock_on_release;
static int32_t release_and_lock(void *ctx, uint64_t token, uint32_t ms) {
    int32_t rc = release_timed(ctx, token, ms);
    if (lock_on_release && releases == 1) assert(RuntimeStreams::Testing::lockRegistry());
    return rc;
}
static unsigned event_mode;
static uint64_t controller_clock(void *ctx) { assert(ctx==(void *)1); return tick; }
static int32_t controller_event(void *, risc_usb_controller_event_v1 *out, uint32_t budget) {
    assert(budget && budget <= 1000);
    if (event_mode == 0) { *out = {1, 2}; event_mode = 1; return 1; }
    if (event_mode == 2) { *out = {2, 2}; event_mode = 3; return 1; }
    if (event_mode == 3) { *out = {1, 3}; event_mode = 4; return 1; }
    return 0;
}
static int32_t controller_old_event(void *, risc_usb_controller_event_v1 *) { assert(false); return 0; }
static bool controller_quiesce(void *) { return !physical[0]; }
static int32_t controller_control(void *ctx, uint64_t target, uint8_t type, uint8_t req,
                                  uint16_t value, uint16_t index, uint8_t *bytes,
                                  uint16_t length, uint32_t budget) {
    assert(target == device); return control(ctx, 100, type, req, value, index, bytes, length, budget);
}
static risc_usb_controller_deadline_api_v1 lower_timed = {
    1, sizeof(lower_timed), controller_clock, controller_event, configuration_timed, claim_timed,
    release_and_lock, controller_control, bulk_read, bulk_write
};
static risc_usb_controller_deadlines_v1 lower = {
    {{{1,sizeof(lower),(void *)1,controller_old_event,configuration,claim,release_checked,
        controller_control,bulk_read,bulk_write,controller_quiesce},nullptr},nullptr},
    RISC_USB_CONTROLLER_DEADLINES_TAG_V1,1,&lower_timed
};
int main(int argc, char **argv) {
    assert(argc == 4);
    void *lib=dlopen(argv[1],RTLD_NOW); assert(lib);
    auto get=(risc_driver_get_v2_fn)dlsym(lib,"t5_driver_get"); assert(get);
    driver=get(2);
    auto ext=(const risc_driver_stream_sessions_v2 *)driver;
    adapter=ext->stream_sessions; poll_driver=&ext->poll;
    void *host_lib=dlopen(argv[3],RTLD_NOW|RTLD_LOCAL); assert(host_lib);
    auto host_get=(risc_driver_get_v2_fn)dlsym(host_lib,"t5_driver_get"); assert(host_get);
    const auto *host_driver=host_get(2); assert(host_driver);
    risc_provider_dependency_v1 host_dependency={"usb.controller",1,&lower};
    assert(host_driver->start(&host_dependency,1));
    const auto *real_host=(const risc_usb_host_deadlines_v1 *)host_driver->capability;
    assert(real_host->extension_tag==RISC_USB_HOST_DEADLINES_TAG_V1 && real_host->deadlines);
    uint64_t ids[8]{}; size_t count=8;
    assert(real_host->deadlines->snapshot(nullptr,ids,&count,250)==0 && count==1);
    request.provider_device=request.device_generation=ids[0];
    const auto *queue_host=RuntimeStreams::runtimeProviderStreamHost();
    risc_stream_provider_v1 real{}; assert(queue_host->open(&real));
    assert(poll_driver->streams.bind_streams(&real));
    risc_provider_dependency_v1 dependency={"usb.host",1,real_host}; assert(driver->start(&dependency,1));
    const bool stale=!strcmp(argv[2],"stale");
    const bool io_retained=!strcmp(argv[2],"io-retained");
    const bool deadline=!strcmp(argv[2],"deadline");
    const bool partial=!strcmp(argv[2],"partial-open");
    const bool retained=!strcmp(argv[2],"close-busy");
    uint32_t existing[3]{};
    if(deadline) {
        assert(open_session(30)==RISC_STREAM_RETAINED && opened.session && physical[0] && !controls && !releases);
        assert(calls==3 && seen_ms[0]==30 && seen_ms[1]==20 && seen_ms[2]==10);
        assert(!driver->quiesce() && !host_driver->quiesce());
        driver->stop(); host_driver->stop(); assert(!releases);
        puts("Vendor + production host + Runtime queues: total open deadline custody PASS");
        return 0;
    }
    if(partial) {
        const risc_stream_endpoint_v1 spec={sizeof(spec),1,RISC_STREAM_READ,32,nullptr,0,0};
        for(auto &id:existing) assert(real.publish(real.context,&spec,&id)==0);
        assert(open_session(250)==RISC_STREAM_IO && !opened.session && !opened.rx_endpoint && !opened.tx_endpoint);
        assert(!physical[0] && driver->quiesce());
        assert(RuntimeStreams::Testing::allocatedBytes()==96);
        for(auto id:existing) assert(real.close(real.context,id)==0);
    } else {
        assert(open_session(250)==0);
        assert(calls==3+OPEN_CONTROLS);
        for (size_t i=0;i<calls;++i) assert(seen_ms[i]==250-10*i);
        const uint64_t provider_token=opened.session;
        const uint64_t lease=400,session=500; const uint32_t owner=1;
        assert(RuntimeStreams::reserveEndpointPair(real.context,lease,owner,session,opened.rx_endpoint,opened.tx_endpoint)==0);
        assert(queue_host->grant(real.context,lease,owner,opened.rx_endpoint,RISC_STREAM_READ));
        assert(queue_host->grant(real.context,lease,owner,opened.tx_endpoint,RISC_STREAM_WRITE));
        uint32_t n=77; char data[8]{};
        assert(RuntimeStreams::providerStreamWrite(real.context,lease,2,opened.tx_endpoint,"12345",5,&n)==RISC_STREAM_DENIED && !n);
        assert(RuntimeStreams::providerStreamWrite(real.context,lease,owner,opened.tx_endpoint,"12345",5,&n)==0 && n==5);
        if (io_retained) {
            charge=0; bulk_result=RISC_STREAM_RETAINED; poll_driver->poll(250);
            assert(!driver->quiesce() && !host_driver->quiesce() && physical[0]);
            assert(queue_host->safe && !queue_host->safe(real.context));
            assert(RuntimeStreams::providerStreamRead(real.context,lease,owner,opened.rx_endpoint,data,8,&n)==RISC_STREAM_CLOSED && !n);
            assert(RuntimeStreams::providerStreamWrite(real.context,lease,owner,opened.tx_endpoint,"x",1,&n)==RISC_STREAM_CLOSED && !n);
            assert(adapter->close(provider_token,250)==RISC_STREAM_RETAINED && !releases);
            host_driver->stop(); driver->stop(); assert(!releases);
            puts("Vendor + production host + Runtime queues: retained lower I/O PASS");
            return 0;
        }
        charge=0;
        for(unsigned i=0;i<3;++i) poll_driver->poll(250);
        assert(sent_size==5 && !memcmp(sent,"12345",5));
        assert(RuntimeStreams::providerStreamRead(real.context,lease,2,opened.rx_endpoint,data,8,&n)==RISC_STREAM_DENIED && !n);
        assert(RuntimeStreams::providerStreamRead(real.context,lease,owner,opened.rx_endpoint,data,8,&n)==0 && n==5 && !memcmp(data,"abcde",5));
        assert(RuntimeStreams::releaseEndpointPair(real.context,lease,2,session,opened.rx_endpoint,opened.tx_endpoint)==RISC_STREAM_DENIED);
        assert(RuntimeStreams::revokeProviderStreamGrant(real.context,lease)==0);
        int before_controls=controls;
        if (stale) {
            event_mode=2; device=3;
            risc_serial_stream_call_v1 req={1,sizeof(req),RISC_SERIAL_STREAM_CHECK_DEVICE,0,{{0}}};
            uint32_t actual=99;
            assert(adapter->call(provider_token,&req,sizeof(req),250,nullptr,0,&actual)==RISC_STREAM_DISCONNECTED && !actual);
        }
        lock_on_release=retained;
        int32_t rc=adapter->close(provider_token,250);
        if(stale) assert(controls==before_controls);
        if(retained) {
            assert(rc==RISC_STREAM_RETAINED && !physical[0] && !driver->quiesce());
            RuntimeStreams::Testing::unlockRegistry();
            assert(RuntimeStreams::Testing::allocatedBytes()==4096);
            assert(adapter->close(provider_token,250)==RISC_STREAM_RETAINED && releases==1);
            assert(queue_host->revokeChecked(real.context));
            assert(RuntimeStreams::providerStreamRead(real.context,lease,owner,opened.rx_endpoint,data,8,&n)==RISC_STREAM_CLOSED);
            puts("Vendor + production host + Runtime queues: close-busy retained PASS");
            return 0; // Deliberately retain mapped provider and queue custody.
        }
        assert(rc==0 && driver->quiesce() && RuntimeStreams::Testing::allocatedBytes()==4096);
        assert(RuntimeStreams::providerStreamRead(real.context,lease,owner,opened.rx_endpoint,data,8,&n)==RISC_STREAM_CLOSED);
        assert(RuntimeStreams::releaseEndpointPair(real.context,lease,owner,session,opened.rx_endpoint,opened.tx_endpoint)==0);
        assert(adapter->close(provider_token,250)==RISC_STREAM_CLOSED);
    }
    assert(RuntimeStreams::Testing::allocatedBytes()==0);
    driver->stop(); assert(queue_host->revokeChecked(real.context)); assert(queue_host->closeChecked(real.context)); assert(dlclose(lib)==0);
    assert(host_driver->quiesce()); host_driver->stop(); assert(dlclose(host_lib)==0);
    printf("Vendor + production host + Runtime queues: %s PASS\n",argv[2]);
}
