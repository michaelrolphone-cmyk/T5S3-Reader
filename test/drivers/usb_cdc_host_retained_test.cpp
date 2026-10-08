/* Actual CDC + actual deadline host + production Runtime queues. Only the
 * physical controller is deterministic. No hardware or PHY/power claim. */
#include "RiscUsbHostDeadlinesV1.h"
#include "RiscUsbControllerDeadlinesV1.h"
#include "RiscStreamSessionProviderV1.h"
#include "RiscSerialStreamSessionV1.h"
#include "runtime/streams/ProviderQueueHost.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>

static const uint8_t descriptor[]={9,2,41,0,2,1,0,0x80,50,9,4,0,0,0,2,2,1,0,
    9,4,1,0,2,10,0,0,0,7,5,0x81,2,64,0,0,7,5,0x02,2,64,0,0};
static bool attached,physical[2],read_once,retained_seen,notification_busy;
static unsigned events,configurations,claims,releases,controls,reads,writes,clock_calls;
static unsigned host_calls,produces,consumes,publishes,closes,finishes;
static uint32_t produced,consumed,written;
static int fault; // 1=control, 2=read, 3=write, 4/5=open rollback release
static uint64_t now(void*) { ++clock_calls; return 0; }
static int32_t event(void*,risc_usb_controller_event_v1 *out,uint32_t ms) {
    assert(ms); ++events;
    if(attached) return 0;
    attached=true; *out={1,42}; return 1;
}
static int32_t config(void*,uint64_t device,uint8_t *out,size_t *size,uint16_t *vid,uint16_t *pid,uint32_t ms) {
    assert(device==42 && ms && *size>=sizeof(descriptor)); ++configurations;
    std::memcpy(out,descriptor,sizeof(descriptor)); *size=sizeof(descriptor); *vid=0x1234; *pid=0x5678; return 0;
}
static int32_t claim(void*,uint64_t device,uint8_t iface,uint8_t alt,uint64_t *out,uint32_t ms) {
    assert(device==42 && iface<2 && !alt && !physical[iface] && ms); ++claims;
    physical[iface]=true; *out=100+iface; return 0;
}
static int32_t release(void*,uint64_t id,uint32_t ms) {
    assert((fault==4 || fault==5) && id==101 && ms); ++releases;
    return RISC_STREAM_RETAINED;
}
static int32_t control(void*,uint64_t device,uint8_t type,uint8_t req,uint16_t,uint16_t index,uint8_t*,uint16_t n,uint32_t ms) {
    assert(device==42 && type==0x21 && !index && ms && physical[0] && physical[1]);
    assert((req==0x20 && n==7)||(req==0x22 && !n)); ++controls;
    if(fault==4) return RISC_STREAM_IO;
    return fault==1 ? int32_t(RISC_STREAM_RETAINED) : int32_t(n);
}
static int32_t read_data(void*,uint64_t id,uint8_t ep,uint8_t *out,size_t n,uint32_t ms) {
    assert(id==101 && ep==0x81 && n==256 && ms==1 && physical[1]); ++reads;
    if(fault==2) return RISC_STREAM_RETAINED;
    if(read_once) return 0;
    read_once=true; std::memcpy(out,"abcde",5); return 5;
}
static int32_t write_data(void*,uint64_t id,uint8_t ep,const uint8_t *src,size_t n,uint32_t ms) {
    assert(id==101 && ep==2 && n && src && ms==1 && physical[1]); ++writes;
    if(fault==3) return RISC_STREAM_RETAINED;
    if(n>2)n=2;
    assert(!std::memcmp(src,"12345"+written,n)); written+=uint32_t(n); return int32_t(n);
}
static int32_t old_event(void*,risc_usb_controller_event_v1*) { assert(false); return 0; }
static bool old_config(void*,uint64_t,uint8_t*,size_t*,uint16_t*,uint16_t*) { assert(false); return false; }
static bool old_claim(void*,uint64_t,uint8_t,uint8_t,uint64_t*) { assert(false); return false; }
static bool old_release(void*,uint64_t) { assert(false); return false; }
static int32_t old_control(void*,uint64_t,uint8_t,uint8_t,uint16_t,uint16_t,uint8_t*,uint16_t,uint32_t) { assert(false); return -1; }
static int32_t old_read(void*,uint64_t,uint8_t,uint8_t*,size_t,uint32_t) { assert(false); return -1; }
static int32_t old_write(void*,uint64_t,uint8_t,const uint8_t*,size_t,uint32_t) { assert(false); return -1; }
static bool quiesce(void*) { assert(false); return false; }
static risc_usb_controller_deadline_api_v1 controller_api={1,sizeof(controller_api),now,event,config,claim,release,control,read_data,write_data};
static risc_usb_controller_deadlines_v1 controller={
    {{{1,sizeof(controller),nullptr,old_event,old_config,old_claim,old_release,old_control,old_read,old_write,quiesce},nullptr},nullptr},
    RISC_USB_CONTROLLER_DEADLINES_TAG_V1,1,&controller_api};
static const risc_usb_host_deadlines_v1 *real_host;
static risc_stream_provider_v1 real_queues;
static void host_enter() { assert(!retained_seen); ++host_calls; }
static int32_t host_result(int32_t rc) {
    if(rc==RISC_STREAM_RETAINED) {
        retained_seen=true;
        if(notification_busy) assert(RuntimeStreams::Testing::lockRegistry());
    }
    return rc;
}
static uint64_t host_now(void *ctx) { host_enter(); return real_host->deadlines->now_ms(ctx); }
static int32_t host_snapshot(void *ctx,uint64_t *out,size_t *n,uint32_t ms) { host_enter(); return host_result(real_host->deadlines->snapshot(ctx,out,n,ms)); }
static int32_t host_config(void *ctx,uint64_t d,uint8_t *out,size_t *n,uint16_t *v,uint16_t *p,uint32_t ms) { host_enter(); return host_result(real_host->deadlines->configuration(ctx,d,out,n,v,p,ms)); }
static int32_t host_claim(void *ctx,uint64_t d,uint8_t i,uint8_t a,uint64_t *out,uint32_t ms) { host_enter(); return host_result(real_host->deadlines->claim(ctx,d,i,a,out,ms)); }
static int32_t host_release(void *ctx,uint64_t c,uint32_t ms) { host_enter(); return host_result(real_host->deadlines->release(ctx,c,ms)); }
static int32_t host_control(void *ctx,uint64_t c,uint8_t t,uint8_t r,uint16_t v,uint16_t i,uint8_t *p,uint16_t n,uint32_t ms) { host_enter(); return host_result(real_host->snapshot.discovery.control_claim(ctx,c,t,r,v,i,p,n,ms)); }
static int32_t host_read(void *ctx,uint64_t c,uint8_t ep,uint8_t *p,size_t n,uint32_t ms) { host_enter(); return host_result(real_host->snapshot.discovery.host.bulk_read(ctx,c,ep,p,n,ms)); }
static int32_t host_write(void *ctx,uint64_t c,uint8_t ep,const uint8_t *p,size_t n,uint32_t ms) { host_enter(); return host_result(real_host->snapshot.discovery.host.bulk_write(ctx,c,ep,p,n,ms)); }
static int32_t publish(uint64_t ctx,const risc_stream_endpoint_v1 *spec,uint32_t *out) {
    assert(!retained_seen); ++publishes;
    if(fault==5 && publishes==2) return RISC_STREAM_LIMIT;
    return real_queues.publish(ctx,spec,out);
}
static int32_t produce(uint64_t ctx,uint32_t id,const void *p,uint32_t n,uint32_t *out) {
    assert(!retained_seen); ++produces; if(n>2)n=2;
    int32_t rc=real_queues.produce(ctx,id,p,n,out); produced+=*out; return rc;
}
static int32_t consume(uint64_t ctx,uint32_t id,void *p,uint32_t n,uint32_t *out) {
    assert(!retained_seen); ++consumes;
    int32_t rc=real_queues.consume(ctx,id,p,n,out); consumed+=*out; return rc;
}
static int32_t close_queue(uint64_t ctx,uint32_t id) { assert(!retained_seen); ++closes; return real_queues.close(ctx,id); }
static int32_t finish(uint64_t ctx,uint32_t id,int32_t rc) {
    assert(retained_seen && rc==RISC_STREAM_RETAINED); ++finishes;
    return real_queues.finish(ctx,id,rc);
}
int main(int argc,char **argv) {
    assert(argc==4);
    void *host_lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); assert(host_lib);
    auto host_get=(risc_driver_get_v2_fn)dlsym(host_lib,"t5_driver_get"); assert(host_get);
    const risc_driver_v2 *host_driver=host_get(2);
    risc_provider_dependency_v1 controller_dep={"usb.controller",1,&controller};
    assert(host_driver->start(&controller_dep,1));
    real_host=(const risc_usb_host_deadlines_v1 *)host_driver->capability;
    assert(real_host->extension_tag==RISC_USB_HOST_DEADLINES_TAG_V1 && real_host->deadlines);
    risc_usb_host_deadlines_v1 copied=*real_host;
    risc_usb_host_deadline_api_v1 wrapped={1,sizeof(wrapped),host_now,host_snapshot,host_config,host_claim,host_release};
    copied.deadlines=&wrapped; copied.snapshot.discovery.control_claim=host_control;
    copied.snapshot.discovery.host.bulk_read=host_read; copied.snapshot.discovery.host.bulk_write=host_write;
    uint64_t ids[8]{};size_t count=8;
    assert(copied.deadlines->snapshot(nullptr,ids,&count,250)==0 && count==1);
    const auto *qh=RuntimeStreams::runtimeProviderStreamHost(); assert(qh->open(&real_queues));
    risc_stream_provider_v1 queues=real_queues;
    queues.publish=publish;queues.produce=produce;queues.consume=consume;queues.close=close_queue;queues.finish=finish;
    void *cdc_lib=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL); assert(cdc_lib);
    auto get=(risc_driver_get_v2_fn)dlsym(cdc_lib,"t5_driver_get"); assert(get);
    auto driver=(const risc_driver_stream_sessions_v2 *)get(2);
    const auto *adapter=driver->stream_sessions;
    assert(driver->poll.streams.bind_streams(&queues));
    risc_provider_dependency_v1 host_dep={"usb.host",1,&copied};
    assert(driver->poll.streams.driver.start(&host_dep,1));
    risc_serial_stream_open_v1 req={1,sizeof(req),ids[0],ids[0],{115200,8,0,1,0}};
    risc_provider_stream_session_v1 opened{sizeof(opened)};
    bool rollback=!std::strcmp(argv[3],"rollback-release");
    bool partial=!std::strcmp(argv[3],"partial-open-release");
    bool opening=!std::strcmp(argv[3],"open-control") || rollback || partial;
    notification_busy=!std::strcmp(argv[3],"read-notify-busy");
    if(opening) fault=rollback ? 4 : partial ? 5 : 1;
    int32_t rc=adapter->open(&req,sizeof(req),250,&opened);
    if(opening) {
        assert(rc==RISC_STREAM_RETAINED && opened.session && !opened.tx_endpoint);
        if(partial) {
            assert(opened.rx_endpoint && publishes==2 && finishes==1);
            assert(RuntimeStreams::Testing::allocatedBytes()==2048 && !qh->safe(queues.context));
        } else assert(!opened.rx_endpoint && !publishes && !finishes && RuntimeStreams::Testing::allocatedBytes()==0);
    } else {
        assert(rc==0 && opened.session && publishes==2);
        assert(RuntimeStreams::reserveEndpointPair(queues.context,400,1,500,opened.rx_endpoint,opened.tx_endpoint)==0);
        assert(qh->grant(queues.context,400,1,opened.rx_endpoint,RISC_STREAM_READ));
        assert(qh->grant(queues.context,400,1,opened.tx_endpoint,RISC_STREAM_WRITE));
        uint32_t n=0;
        assert(RuntimeStreams::providerStreamWrite(queues.context,400,1,opened.tx_endpoint,"12345",5,&n)==0 && n==5);
        driver->poll.poll(250);
        assert(produced==2 && consumed==5 && written==2); // Partial RX and TX staging.
        if(!std::strcmp(argv[3],"configure") || !std::strcmp(argv[3],"lines")) {
            fault=1;
            risc_serial_stream_call_v1 call{1,sizeof(call),RISC_SERIAL_STREAM_CONFIGURE,0,{}};
            call.value.config=req.config;
            if(!std::strcmp(argv[3],"lines")) {
                call.operation=RISC_SERIAL_STREAM_CONTROL_LINES; call.value={};call.value.lines.dtr=1;
            }
            assert(adapter->call(opened.session,&call,sizeof(call),250,nullptr,0,&n)==RISC_STREAM_RETAINED && !n);
            assert(produced==2 && consumed==5 && written==2);
        } else if(!std::strcmp(argv[3],"read") || notification_busy) {
            fault=2; driver->poll.poll(250); driver->poll.poll(250);
            assert(produced==5 && consumed==5 && written==4);
        } else {
            assert(!std::strcmp(argv[3],"write")); fault=3;driver->poll.poll(250);
            assert(produced==4 && consumed==5 && written==2);
        }
        assert(finishes==2);
        if(notification_busy) {
            RuntimeStreams::Testing::unlockRegistry();
            // The terminal callback could not take the queue lock. The local
            // and physical-host fences remain sticky; a control response lets
            // Runtime revoke its invocation without retrying any driver work.
            assert(!driver->poll.streams.driver.quiesce());
            assert(adapter->close(opened.session,250)==RISC_STREAM_RETAINED);
            assert(qh->revokeChecked(queues.context));
        } else assert(!qh->safe(queues.context));
        assert(RuntimeStreams::Testing::allocatedBytes()==4096);
        char data=0;
        assert(RuntimeStreams::providerStreamRead(queues.context,400,1,opened.rx_endpoint,&data,1,&n)==RISC_STREAM_CLOSED);
        assert(RuntimeStreams::providerStreamWrite(queues.context,400,1,opened.tx_endpoint,"X",1,&n)==RISC_STREAM_CLOSED);
    }
    assert(retained_seen && physical[0] && physical[1] && releases==unsigned(rollback || partial) && !closes);
    unsigned host_before=host_calls,clock_before=clock_calls,produced_before=produces,consumed_before=consumes,finished_before=finishes;
    assert(!driver->poll.streams.driver.quiesce());driver->poll.streams.driver.stop();driver->poll.poll(250);
    assert(adapter->close(opened.session,250)==RISC_STREAM_RETAINED);
    risc_provider_stream_session_v1 another{sizeof(another)};
    assert(adapter->open(&req,sizeof(req),250,&another)==RISC_STREAM_RETAINED);
    assert(!another.session && !another.rx_endpoint && !another.tx_endpoint);
    assert(!host_driver->quiesce());host_driver->stop();
    assert(host_calls==host_before && clock_calls==clock_before && produces==produced_before && consumes==consumed_before && finishes==finished_before);
    printf("CDC + production host + Runtime queues explicit RETAINED: %s PASS\n",argv[3]);
    return 0; // Retained mappings and physical/queue custody deliberately survive.
}
