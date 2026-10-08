// The same deterministic USB host drives the real class and production Runtime
// queues. This is a software custody test, not physical host qualification.
#define main cdc_callback_fixture_main
#define _Static_assert static_assert
#include "usb_vendor_tagged_sessions_test.c"
#undef _Static_assert
#undef main
#include "runtime/streams/ProviderQueueHost.h"

static bool lock_on_release;
static int32_t release_and_lock(void *ctx, uint64_t token, uint32_t ms) {
    int32_t rc = release_timed(ctx, token, ms);
    if (lock_on_release && releases == 1) assert(RuntimeStreams::Testing::lockRegistry());
    return rc;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    void *lib=dlopen(argv[1],RTLD_NOW); assert(lib);
    auto get=(risc_driver_get_v2_fn)dlsym(lib,"t5_driver_get"); assert(get);
    driver=get(2);
    auto ext=(const risc_driver_stream_sessions_v2 *)driver;
    adapter=ext->stream_sessions; poll_driver=&ext->poll;
    host.snapshot.discovery.host={1,sizeof(host),(void *)1,configuration,claim,release_legacy,control,bulk_read,bulk_write};
    host.snapshot.discovery.poll=poll; host.snapshot.discovery.devices=devices;
    host.snapshot.discovery.release_checked=release_checked; host.snapshot.discovery.control_claim=control;
    host.extension_tag=RISC_USB_HOST_DEADLINES_TAG_V1; host.extension_version=1; host.deadlines=&timed;
    timed.release=release_and_lock;
    const auto *queue_host=RuntimeStreams::runtimeProviderStreamHost();
    risc_stream_provider_v1 real{}; assert(queue_host->open(&real));
    assert(poll_driver->streams.bind_streams(&real));
    risc_provider_dependency_v1 dependency={"usb.host",1,&host}; assert(driver->start(&dependency,1));
    const bool partial=!strcmp(argv[2],"partial-open");
    const bool retained=!strcmp(argv[2],"close-busy");
    uint32_t existing[3]{};
    if(partial) {
        const risc_stream_endpoint_v1 spec={sizeof(spec),1,RISC_STREAM_READ,32,nullptr,0,0};
        for(auto &id:existing) assert(real.publish(real.context,&spec,&id)==0);
        assert(open_session(250)==RISC_STREAM_IO && !opened.session && !opened.rx_endpoint && !opened.tx_endpoint);
        assert(!physical[0] && driver->quiesce());
        assert(RuntimeStreams::Testing::allocatedBytes()==96);
        for(auto id:existing) assert(real.close(real.context,id)==0);
    } else {
        assert(open_session(250)==0);
        const uint64_t provider_token=opened.session;
        const uint64_t lease=400,session=500; const uint32_t owner=1;
        assert(RuntimeStreams::reserveEndpointPair(real.context,lease,owner,session,opened.rx_endpoint,opened.tx_endpoint)==0);
        assert(queue_host->grant(real.context,lease,owner,opened.rx_endpoint,RISC_STREAM_READ));
        assert(queue_host->grant(real.context,lease,owner,opened.tx_endpoint,RISC_STREAM_WRITE));
        uint32_t n=77; char data[8]{};
        assert(RuntimeStreams::providerStreamWrite(real.context,lease,2,opened.tx_endpoint,"12345",5,&n)==RISC_STREAM_DENIED && !n);
        assert(RuntimeStreams::providerStreamWrite(real.context,lease,owner,opened.tx_endpoint,"12345",5,&n)==0 && n==5);
        charge=0;
        for(unsigned i=0;i<3;++i) poll_driver->poll(250);
        assert(sent_size==5 && !memcmp(sent,"12345",5));
        assert(RuntimeStreams::providerStreamRead(real.context,lease,2,opened.rx_endpoint,data,8,&n)==RISC_STREAM_DENIED && !n);
        assert(RuntimeStreams::providerStreamRead(real.context,lease,owner,opened.rx_endpoint,data,8,&n)==0 && n==5 && !memcmp(data,"abcde",5));
        assert(RuntimeStreams::releaseEndpointPair(real.context,lease,2,session,opened.rx_endpoint,opened.tx_endpoint)==RISC_STREAM_DENIED);
        assert(RuntimeStreams::revokeProviderStreamGrant(real.context,lease)==0);
        lock_on_release=retained;
        int32_t rc=adapter->close(provider_token,250);
        if(retained) {
            assert(rc==RISC_STREAM_RETAINED && !physical[0] && !driver->quiesce());
            RuntimeStreams::Testing::unlockRegistry();
            assert(RuntimeStreams::Testing::allocatedBytes()==4096);
            assert(adapter->close(provider_token,250)==RISC_STREAM_RETAINED && releases==1);
            assert(queue_host->revokeChecked(real.context));
            assert(RuntimeStreams::providerStreamRead(real.context,lease,owner,opened.rx_endpoint,data,8,&n)==RISC_STREAM_CLOSED);
            puts("Vendor production Runtime queues: close-busy retained PASS");
            return 0; // Deliberately retain mapped provider and queue custody.
        }
        assert(rc==0 && driver->quiesce() && RuntimeStreams::Testing::allocatedBytes()==4096);
        assert(RuntimeStreams::providerStreamRead(real.context,lease,owner,opened.rx_endpoint,data,8,&n)==RISC_STREAM_CLOSED);
        assert(RuntimeStreams::releaseEndpointPair(real.context,lease,owner,session,opened.rx_endpoint,opened.tx_endpoint)==0);
        assert(adapter->close(provider_token,250)==RISC_STREAM_CLOSED);
    }
    assert(RuntimeStreams::Testing::allocatedBytes()==0);
    driver->stop(); assert(queue_host->revokeChecked(real.context)); assert(queue_host->closeChecked(real.context)); assert(dlclose(lib)==0);
    printf("Vendor production Runtime queues: %s PASS\n",argv[2]);
}
