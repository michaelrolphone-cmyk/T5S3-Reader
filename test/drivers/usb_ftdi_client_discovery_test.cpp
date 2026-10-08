// Real FTDI ELF + unmodified paper Serial client + production Runtime queues.
// The small broker shim below only maps its issued IDs to the real provider.
#define main vendor_fixture_main
#define _Static_assert static_assert
#include "usb_vendor_tagged_sessions_test.c"
#undef _Static_assert
#undef main
#include "runtime/streams/ProviderQueueHost.h"
extern "C" {
#include "PortableSerialClient.h"
}

static size_t inventory_count=1;
static bool replace_during_scan, reverse_final, descriptor_bad, device_nonmatch, change_at_open;
static bool partial_configuration, configuration_unknown, configuration_retained, retained_discovery, grant_live;
static unsigned broker_opens,broker_calls,broker_closes,grant_releases;
static uint64_t broker_token, issued_token=900, physical_token;
static uint32_t actual_rx,actual_tx;
static risc_stream_provider_v1 real;
static const RuntimeProviders::StreamHostV1 *queue_host;
static portable_serial_client client;
static const t5_serial_config_t coding={115200,8,0,1,0};

static int32_t inventory_snapshot(void *ctx,uint64_t *out,size_t *count,uint32_t ms) {
    assert(ctx==(void *)1 && *count==8); ++snapshots; step('S',ms);
    if(retained_discovery){lower_retained=true;return RISC_STREAM_RETAINED;}
    if(snapshot_mode==1)return RISC_STREAM_IO;
    if(snapshot_mode==4){*count=9;return RISC_STREAM_OK;}
    if(replace_during_scan && snapshots==2)device=3;
    *count=inventory_count;
    for(size_t i=0;i<inventory_count;++i)out[i]=device+i;
    if(snapshot_mode==2 && inventory_count>=2)out[1]=out[0];
    if(snapshot_mode==3 && inventory_count)out[0]=0;
    if(reverse_final && snapshots==2 && inventory_count==2){uint64_t t=out[0];out[0]=out[1];out[1]=t;}
    return RISC_STREAM_OK;
}
static int32_t inventory_configuration(void *ctx,uint64_t token,uint8_t *out,size_t *len,
                                       uint16_t *vid,uint16_t *pid,uint32_t ms) {
    assert(ctx==(void *)1 && token>=device && token<device+inventory_count && *len>=sizeof(descriptor));
    ++configs;step('D',ms);
    if(configuration_retained){lower_retained=true;return RISC_STREAM_RETAINED;}
    if(configuration_unknown || (partial_configuration && token==device+1))return RISC_STREAM_IO;
    memcpy(out,descriptor,sizeof(descriptor));*len=sizeof(descriptor);
    *vid=device_nonmatch?0x1234:0x0403;*pid=0x6001;
    if(descriptor_bad)out[25]=1; // Invalid descriptor length inside the configuration.
    return RISC_STREAM_OK;
}
static int32_t legacy_control(void *ctx,uint64_t token,uint8_t type,uint8_t req,
                              uint16_t value,uint16_t index,uint8_t *bytes,uint16_t length,uint32_t ms) {
    assert(token==device);
    if(type==0x80 && req==6) {
        assert(!physical[0] && value==0x100 && !index && length==18 && ms==1000);
        memset(bytes,0,length);bytes[0]=18;bytes[1]=1;bytes[13]=6;return 18;
    }
    return control(ctx,100,type,req,value,index,bytes,length,ms);
}
static bool acquire_client(const char *cap,uint32_t version,uint64_t instance,risc_runtime_capability_v1 *out) {
    assert(!strcmp(cap,"serial.port")&&version==1&&instance==7&&!grant_live);
    out->api=driver->capability;out->slot=1;out->generation=2;grant_live=true;return true;
}
static bool release_client(risc_runtime_capability_v1 *out) {
    assert(grant_live&&out->api==driver->capability);
    if(!driver->quiesce())return false;
    memset(out,0,sizeof(*out));grant_live=false;++grant_releases;return true;
}
static bool retain_client(void){assert(false);return false;}
static int32_t broker_open(uint64_t context,const risc_runtime_capability_v1 *grant,const void *bytes,
                           uint32_t length,uint32_t budget,risc_stream_opened_v1 *out) {
    assert(context==701&&grant_live&&grant->api==driver->capability&&!broker_token&&budget==250);
    const auto *req=(const risc_serial_stream_open_v1 *)bytes;
    assert(length==sizeof(*req)&&req->provider_device==client.device&&req->device_generation==client.generation);
    assert(req->device_generation==req->provider_device);++broker_opens;
    if(change_at_open)device=3;
    risc_provider_stream_session_v1 result{};result.struct_size=sizeof(result);
    int32_t rc=adapter->open(bytes,length,budget,&result);
    if(rc!=RISC_STREAM_OK)return rc;
    physical_token=result.session;actual_rx=result.rx_endpoint;actual_tx=result.tx_endpoint;broker_token=++issued_token;
    assert(RuntimeStreams::reserveEndpointPair(real.context,400,1,broker_token,actual_rx,actual_tx)==0);
    assert(queue_host->grant(real.context,400,1,actual_rx,RISC_STREAM_READ));
    assert(queue_host->grant(real.context,400,1,actual_tx,RISC_STREAM_WRITE));
    out->session=broker_token;out->rx=broker_token+1000;out->tx=broker_token+2000;
    return RISC_STREAM_OK;
}
static int32_t broker_call(uint64_t ctx,uint64_t token,const void *request,uint32_t size,uint32_t ms,
                           void *reply,uint32_t capacity,uint32_t *actual) {
    assert(ctx==701&&token==broker_token);++broker_calls;
    return adapter->call(physical_token,request,size,ms,reply,capacity,actual);
}
static int32_t broker_close(uint64_t ctx,uint64_t token,uint32_t ms) {
    assert(ctx==701&&token==broker_token);++broker_closes;
    assert(RuntimeStreams::revokeProviderStreamGrant(real.context,400)==0);
    int32_t rc=adapter->close(physical_token,ms);
    if(rc!=RISC_STREAM_OK)return rc;
    assert(RuntimeStreams::releaseEndpointPair(real.context,400,1,token,actual_rx,actual_tx)==0);
    broker_token=physical_token=actual_rx=actual_tx=0;return rc;
}
static int32_t broker_read(uint64_t ctx,uint64_t id,void *out,uint32_t capacity,uint32_t *actual) {
    assert(ctx==701&&id==broker_token+1000);
    return RuntimeStreams::providerStreamRead(real.context,400,1,actual_rx,out,capacity,actual);
}
static int32_t broker_write(uint64_t ctx,uint64_t id,const void *data,uint32_t length,uint32_t *actual) {
    assert(ctx==701&&id==broker_token+2000);
    return RuntimeStreams::providerStreamWrite(real.context,400,1,actual_tx,data,length,actual);
}
static int32_t broker_info(uint64_t,uint64_t,risc_stream_client_info_v1 *){assert(false);return RISC_STREAM_UNSUPPORTED;}
static bool client_streams(risc_stream_client_v1 *out) {
    *out={1,sizeof(*out),701,broker_open,broker_call,broker_close,broker_read,broker_write,broker_info};return true;
}
#ifdef PRODUCTION_USB_HOST
#include "RiscUsbControllerDeadlinesV1.h"
static uint64_t announced[8];
static int32_t controller_event(void *,risc_usb_controller_event_v1 *out,uint32_t ms) {
    assert(ms&&ms<=1000);
    if(snapshot_mode==1)return RISC_STREAM_IO;
    for(size_t i=0;i<8;++i)if(announced[i]&&(announced[i]<device||announced[i]>=device+inventory_count)) {
        *out={2,announced[i]};announced[i]=0;return 1;
    }
    for(size_t i=0;i<inventory_count;++i) {
        bool found=false;for(size_t j=0;j<8;++j)found|=announced[j]==device+i;
        if(!found)for(size_t j=0;j<8;++j)if(!announced[j]){announced[j]=device+i;*out={1,device+i};return 1;}
    }
    return 0;
}
static int32_t controller_old_event(void *,risc_usb_controller_event_v1 *){assert(false);return 0;}
static bool controller_quiesce(void *){return !physical[0];}
static uint64_t controller_clock(void *){return tick;}
static int32_t controller_control(void *ctx,uint64_t target,uint8_t type,uint8_t req,uint16_t value,
                                  uint16_t index,uint8_t *bytes,uint16_t length,uint32_t ms) {
    assert(target==device);return control(ctx,100,type,req,value,index,bytes,length,ms);
}
static risc_usb_controller_deadline_api_v1 lower_timed={1,sizeof(lower_timed),controller_clock,controller_event,
    inventory_configuration,claim_timed,release_timed,controller_control,bulk_read,bulk_write};
static risc_usb_controller_deadlines_v1 lower={
    {{{1,sizeof(lower),(void *)1,controller_old_event,configuration,claim,release_checked,
        controller_control,bulk_read,bulk_write,controller_quiesce},nullptr},nullptr},
    RISC_USB_CONTROLLER_DEADLINES_TAG_V1,1,&lower_timed};
#endif
static void assert_no_claims(void) {assert(!claims&&!controls&&!publishes&&!reads&&!writes&&!physical[0]);}
static void failed_selection(int wanted) {
    assert(!portable_serial_open(&client,7,&coding)&&client.result==wanted);
    assert(!broker_opens&&!client.connected&&!grant_live&&grant_releases==1);assert_no_claims();
}
int main(int argc,char **argv) {
#ifdef PRODUCTION_USB_HOST
    assert(argc==4);
#else
    assert(argc==3);
#endif
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(lib);
    auto get=(risc_driver_get_v2_fn)dlsym(lib,"t5_driver_get");assert(get);
    driver=get(2);const auto *extension=(const risc_driver_stream_sessions_v2 *)driver;
    adapter=extension->stream_sessions;poll_driver=&extension->poll;
    serial=(const risc_usb_cdc_api_v1 *)driver->capability;serial_streams=(const risc_serial_port_streams_v1 *)serial;
    assert(serial->struct_size==sizeof(risc_serial_port_streams_v1)&&serial_streams->inventory.snapshot&&serial_streams->inventory.discovery.probe&&serial_streams->endpoints);
    static_assert(offsetof(risc_serial_port_discovery_v1,probe)==sizeof(risc_serial_port_api_v1),"raw prefix");
    static_assert(offsetof(risc_serial_port_inventory_v1,snapshot)==sizeof(risc_serial_port_discovery_v1),"probe prefix");
    static_assert(offsetof(risc_serial_port_streams_v1,endpoints)==sizeof(risc_serial_port_inventory_v1),"inventory prefix");
    host.snapshot.discovery.host={1,sizeof(host),(void *)1,configuration,claim,release_legacy,legacy_control,bulk_read,bulk_write};
    host.snapshot.discovery.poll=poll;host.snapshot.discovery.devices=devices;
    host.snapshot.discovery.release_checked=release_checked;host.snapshot.discovery.control_claim=control;
    host.extension_tag=RISC_USB_HOST_DEADLINES_TAG_V1;host.extension_version=1;host.deadlines=&timed;
    timed.snapshot=inventory_snapshot;timed.configuration=inventory_configuration;
    queue_host=RuntimeStreams::runtimeProviderStreamHost();assert(queue_host->open(&real));
    assert(poll_driver->streams.bind_streams(&real));
    const void *host_api=&host;
#ifdef PRODUCTION_USB_HOST
    void *host_lib=dlopen(argv[3],RTLD_NOW|RTLD_LOCAL);assert(host_lib);
    auto host_get=(risc_driver_get_v2_fn)dlsym(host_lib,"t5_driver_get");assert(host_get);
    const risc_driver_v2 *host_driver=host_get(2);risc_provider_dependency_v1 lower_dep={"usb.controller",1,&lower};
    assert(host_driver->start(&lower_dep,1));host_api=host_driver->capability;
#endif
    risc_provider_dependency_v1 dep={"usb.host",1,host_api};assert(driver->start(&dep,1));
    risc_runtime_api_v1 runtime{};runtime.api_version=1;runtime.struct_size=sizeof(runtime);
    runtime.acquire=acquire_client;runtime.release=release_client;runtime.retain_invocation=retain_client;runtime.stream_client=client_streams;
    assert(portable_serial_init(&client,&runtime));
    const char *scenario=argv[2];
    if(!strcmp(scenario,"zero")){inventory_count=0;failed_selection(PSC_NO_DEVICE);}
    else if(!strcmp(scenario,"multiple")){inventory_count=2;failed_selection(PSC_AMBIGUOUS);}
    else if(!strcmp(scenario,"eight")){inventory_count=8;failed_selection(PSC_AMBIGUOUS);assert(configs==8);
#ifndef PRODUCTION_USB_HOST
        assert(snapshots==2);
#endif
    }
    else if(!strcmp(scenario,"unknown")){snapshot_mode=1;failed_selection(PSC_IO);}
    else if(!strcmp(scenario,"duplicate")){inventory_count=2;snapshot_mode=2;failed_selection(PSC_IO);}
    else if(!strcmp(scenario,"zero-token")){snapshot_mode=3;failed_selection(PSC_IO);}
    else if(!strcmp(scenario,"overflow")){snapshot_mode=4;failed_selection(PSC_IO);}
    else if(!strcmp(scenario,"configuration-unknown")){configuration_unknown=true;failed_selection(PSC_IO);}
    else if(!strcmp(scenario,"partial-unknown")){inventory_count=2;partial_configuration=true;failed_selection(PSC_IO);assert(configs==2&&!broker_opens);}
    else if(!strcmp(scenario,"malformed")){descriptor_bad=true;failed_selection(PSC_IO);}
    else if(!strcmp(scenario,"nonmatch")){device_nonmatch=true;failed_selection(PSC_NO_DEVICE);}
    else if(!strcmp(scenario,"changed-during-scan")){replace_during_scan=true;failed_selection(PSC_IO);}
    else if(!strcmp(scenario,"reordered")){inventory_count=2;reverse_final=true;failed_selection(PSC_AMBIGUOUS);}
    else if(!strcmp(scenario,"deadline")){charge=125;failed_selection(PSC_IO);assert(calls==2&&seen_ms[0]==250&&seen_ms[1]==125);}
    else if(!strcmp(scenario,"legacy-host")) {
        host.snapshot.discovery.host.struct_size=sizeof(risc_usb_host_api_v1);failed_selection(PSC_IO);assert(!calls);
    } else if(!strcmp(scenario,"capacity")) {
        inventory_count=2;risc_serial_device_v1 rows[2];memset(rows,0xa5,sizeof(rows));unsigned char before[sizeof(rows)];memcpy(before,rows,sizeof(rows));size_t count=1;
        assert(!serial_streams->inventory.snapshot(rows,&count)&&count==2&&!memcmp(before,rows,sizeof(rows)));assert_no_claims();
    } else if(!strcmp(scenario,"probe")) {
        assert(serial_streams->inventory.discovery.probe(2)==1);assert_no_claims();
        device_nonmatch=true;assert(serial_streams->inventory.discovery.probe(2)==0);
        snapshot_mode=1;assert(serial_streams->inventory.discovery.probe(2)<0);
    } else if(!strcmp(scenario,"stale-before-open")) {
        change_at_open=true;assert(!portable_serial_open(&client,7,&coding)&&client.result==PSC_DISCONNECTED);
        assert(broker_opens==1&&grant_releases==1&&!grant_live);assert_no_claims();
    } else if(!strcmp(scenario,"retained")||!strcmp(scenario,"configuration-retained")||!strcmp(scenario,"overrun")) {
        retained_discovery=!strcmp(scenario,"retained");configuration_retained=!strcmp(scenario,"configuration-retained");over_time=!strcmp(scenario,"overrun");
        assert(!portable_serial_open(&client,7,&coding)&&client.cleanup_required&&grant_live&&!grant_releases);assert_no_claims();
        int before=snapshots+configs+controls+claims+releases;
        assert(!driver->quiesce());driver->stop();size_t count=8;risc_serial_device_v1 rows[8];
        assert(!serial_streams->inventory.snapshot(rows,&count)&&serial_streams->inventory.discovery.probe(2)<0);
        assert(before==snapshots+configs+controls+claims+releases);printf("FTDI actual paper-client discovery: %s retained PASS\n",scenario);return 0;
    } else if(!strcmp(scenario,"legacy-streams")) {
        charge=0;uint64_t raw=serial->open(2);assert(raw&&serial->configure(raw,115200,8,0,1));
        uint32_t rx=0,tx=0;assert(serial_streams->endpoints(raw,&rx,&tx)&&rx&&tx&&rx!=tx);
        assert(queue_host->grant(real.context,400,1,rx,RISC_STREAM_READ));assert(queue_host->grant(real.context,400,1,tx,RISC_STREAM_WRITE));
        uint32_t actual=0;assert(RuntimeStreams::providerStreamWrite(real.context,400,1,tx,"12345",5,&actual)==0&&actual==5);
        for(int i=0;i<3;++i)poll_driver->poll(250);
        char bytes[8]{};assert(RuntimeStreams::providerStreamRead(real.context,400,1,rx,bytes,8,&actual)==0&&actual==5&&!memcmp(bytes,"abcde",5));
        assert(sent_size==5&&!memcmp(sent,"12345",5));assert(serial->close(raw)&&driver->quiesce());
    } else {
        assert(portable_serial_open(&client,7,&coding)&&client.connected&&client.stream_mode&&client.device&&client.generation==client.device&&broker_opens==1);
#ifndef PRODUCTION_USB_HOST
        assert(client.device==2&&calls>=3&&seen_ms[0]==250&&seen_ms[1]==240&&seen_ms[2]==230);
#endif // Inventory has one shared budget.
        charge=0;assert(portable_serial_queue(&client,"hello",0));uint8_t bytes[64]{};
        assert(!portable_serial_tick(&client,bytes,sizeof(bytes),1));
        for(int i=0;i<4;++i)poll_driver->poll(250);
        assert(portable_serial_tick(&client,bytes,sizeof(bytes),2)==5&&!memcmp(bytes,"abcde",5));
        assert(sent_size==7&&!memcmp(sent,"hello\r\n",7));
        if(!strcmp(scenario,"retained-live")) {
            retained_discovery=true;risc_serial_device_v1 rows[8];size_t count=8;
            assert(!serial_streams->inventory.snapshot(rows,&count)&&!driver->quiesce()&&!queue_host->safe(real.context));
            assert(!portable_serial_tick(&client,bytes,sizeof(bytes),3)&&client.cleanup_required&&grant_live);
            assert(physical[0]&&RuntimeStreams::Testing::allocatedBytes()==4096&&!broker_closes&&!grant_releases);
            puts("FTDI actual paper-client discovery: retained-live PASS");return 0;
        } else if(!strcmp(scenario,"unknown-live")) {
            snapshot_mode=1;int before=reads+writes;assert(!portable_serial_tick(&client,bytes,sizeof(bytes),3)&&client.connected&&client.result==PSC_IO);
            poll_driver->poll(250);assert(reads+writes==before&&!broker_closes);
            snapshot_mode=0;portable_serial_tick(&client,bytes,sizeof(bytes),4);assert(client.connected&&client.result==PSC_OK);
        } else if(!strcmp(scenario,"reconnect")) {
            device=3;assert(!portable_serial_tick(&client,bytes,sizeof(bytes),3)&&!client.connected&&client.result==PSC_DISCONNECTED);
            assert(!physical[0]&&broker_closes==1&&grant_releases==1);
            assert(portable_serial_open(&client,7,&coding)&&client.device&&client.generation==client.device&&broker_opens==2);
        } else assert(!strcmp(scenario,"normal"));
        assert(portable_serial_close(&client)&&!grant_live&&driver->quiesce());
    }
    assert(!physical[0]&&RuntimeStreams::Testing::allocatedBytes()==0&&driver->quiesce());
    driver->stop();assert(queue_host->revokeChecked(real.context)&&queue_host->closeChecked(real.context));assert(dlclose(lib)==0);
#ifdef PRODUCTION_USB_HOST
    assert(host_driver->quiesce());host_driver->stop();assert(dlclose(host_lib)==0);
    printf("FTDI actual paper-client + actual host discovery: %s PASS\n",scenario);
#else
    printf("FTDI actual paper-client discovery: %s PASS\n",scenario);
#endif
}
