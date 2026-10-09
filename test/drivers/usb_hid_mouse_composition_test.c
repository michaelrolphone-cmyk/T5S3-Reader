#include "RiscUsbMouseV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscUsbInterruptV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* Exercise the real raw-HID ELF plus the real semantic mouse ELF. The fake
 * host preserves its own quarantine when void release cannot prove cleanup. */
static const uint8_t descriptor[]={0x05,1,0x09,2,0xa1,1,
    0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,
    0x75,5,0x95,1,0x81,1,0x05,1,0x09,0x30,0x09,0x31,
    0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,6,0xc0};
static const uint8_t configuration[]={9,2,34,0,1,1,0,0x80,50,
    9,4,0,0,1,3,0,0,0,
    9,0x21,0x11,1,0,1,0x22,sizeof(descriptor),0,
    7,5,0x81,3,8,0,10};
static bool attached, claimed, quarantine, fail_release, fail_scan;
static uint64_t device=42;
static unsigned pending, releases, claims, read_calls;
static int32_t report_result=3;
static bool host_poll(void *ctx,size_t max,size_t *processed){(void)ctx;assert(max&&processed);*processed=0;return !fail_scan;}
static bool host_devices(void *ctx,uint64_t *out,size_t *n){(void)ctx;assert(*n>=1);*n=attached?1:0;if(attached)out[0]=device;return true;}
static bool host_config(void *ctx,uint64_t dev,uint8_t *out,size_t *n,uint16_t *vid,uint16_t *pid){
    (void)ctx;assert(dev==device&&attached&&*n>=sizeof(configuration));memcpy(out,configuration,sizeof(configuration));
    *n=sizeof(configuration);*vid=0x1234;*pid=0x5678;return true;
}
static bool host_claim(void *ctx,uint64_t dev,uint8_t iface,uint8_t alt,uint64_t *token){
    (void)ctx;assert(dev==device&&attached&&!iface&&!alt&&!claimed&&!quarantine);*token=100;claimed=true;++claims;return true;
}
static void host_release(void *ctx,uint64_t token){(void)ctx;assert(token==100&&claimed);++releases;
    if(fail_release){quarantine=true;return;}claimed=false;
}
static int32_t host_control(void *ctx,uint64_t dev,uint8_t type,uint8_t request,uint16_t value,uint16_t iface,
                            uint8_t *out,uint16_t n,uint32_t timeout){
    (void)ctx;assert(dev==device&&claimed&&type==0x81&&request==6&&value==0x2200&&!iface&&timeout==100&&n==sizeof(descriptor));
    memcpy(out,descriptor,sizeof(descriptor));return sizeof(descriptor);
}
static int32_t host_bulk_read(void *ctx,uint64_t claim,uint8_t ep,uint8_t *out,size_t n,uint32_t ms){
    (void)ctx;(void)claim;(void)ep;(void)out;(void)n;(void)ms;assert(0);return -1;}
static int32_t host_bulk_write(void *ctx,uint64_t claim,uint8_t ep,const uint8_t *out,size_t n,uint32_t ms){
    (void)ctx;(void)claim;(void)ep;(void)out;(void)n;(void)ms;assert(0);return -1;}
static int32_t host_interrupt(void *ctx,uint64_t claim,uint8_t ep,uint8_t *out,size_t n,uint32_t ms){
    (void)ctx;assert(claim==100&&ep==0x81&&n==64&&ms==1&&claimed&&attached);++read_calls;
    if (!pending) return 0;
    --pending; out[0]=1; out[1]=0xff; out[2]=2; return report_result;
}
static const risc_usb_host_interrupt_v1 host={{{1,sizeof(host),0,host_config,host_claim,host_release,
    host_control,host_bulk_read,host_bulk_write},host_poll,host_devices},host_interrupt};
static uint64_t fake_now(void *ctx){(void)ctx;return 100;}
static void fake_sleep(void *ctx,uint32_t ms){(void)ctx;assert(ms==1);}
static const risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),0,fake_now,fake_sleep};
static const risc_driver_v2 *load(const char *path,void **handle){
    *handle=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!*handle){fprintf(stderr,"%s\n",dlerror());assert(0);}
    risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(*handle,"t5_driver_get");assert(get);return get(2);
}
int main(int argc,char **argv){
    assert(argc==3);void *raw_module,*mouse_module;
    const risc_driver_v2 *raw=load(argv[1],&raw_module),*mouse=load(argv[2],&mouse_module);
    const risc_usb_hid_api_v1 *hid=raw->capability;const risc_usb_mouse_api_v1 *pointer=mouse->capability;
    const risc_provider_dependency_v1 hd={"usb.host",1,&host};
    const risc_provider_dependency_v1 md[]={{"usb.hid",1,hid},{"platform.clock",1,&clock_api}};
    assert(raw->start(&hd,1)&&mouse->start(md,2));
    uint64_t sub=pointer->subscribe(0,0);assert(sub);size_t n=0;
    assert(pointer->snapshot(0,sub,0,&n)&&n==0);
    assert(pointer->poll(0,16)&&!claims); /* empty bus is a normal result */
    attached=true;pending=1;assert(pointer->poll(0,16)&&claimed&&claims==1);
    risc_usb_mouse_event_v1 event;
    assert(pointer->next(0,sub,&event)==1&&event.kind==1);
    uint64_t session=event.state.session;
    assert(pointer->next(0,sub,&event)==1&&event.kind==3&&event.x==-1&&event.y==2&&event.pressed==1);
    unsigned before=read_calls;assert(pointer->poll(0,16)&&read_calls==before+1);
    attached=false;assert(pointer->poll(0,16)&&!claimed&&releases==1);
    assert(pointer->next(0,sub,&event)==1&&event.kind==2&&event.released==1&&event.state.session==session);
    assert(pointer->poll(0,1));attached=true;++device;pending=1;assert(pointer->poll(0,16)&&claims==2);
    assert(pointer->next(0,sub,&event)==1&&event.state.session!=session);assert(pointer->next(0,sub,&event)==1);
    pending=1;report_result=-1;assert(!pointer->poll(0,1)&&!claimed&&releases==2);
    assert(pointer->next(0,sub,&event)==-2);n=0;assert(pointer->snapshot(0,sub,0,&n));
    assert(pointer->poll(0,1)&&claims==2); /* read failure does not reclaim same attachment */
    attached=false;assert(pointer->poll(0,1));attached=true;++device;report_result=3;pending=1;
    assert(pointer->poll(0,16)&&claims==3);fail_scan=true;
    assert(!pointer->poll(0,1)&&!claimed&&releases==3);n=0;assert(pointer->snapshot(0,sub,0,&n));fail_scan=false;
    attached=false;assert(pointer->poll(0,1));attached=true;++device;pending=1;assert(pointer->poll(0,16)&&claims==4);
    /* Lost lower-host custody cannot be represented by raw HID's bool close:
     * it invokes void release and clears its token. Verify that no claim is
     * reused and that the HOST's unsafe state remains owned and explicit. */
    fail_release=true;attached=false;assert(pointer->poll(0,1)&&quarantine&&claimed&&releases==4);
    assert(pointer->unsubscribe(0,sub)&&mouse->quiesce()&&raw->quiesce());
    assert(quarantine); /* graph must still refuse lower-host unload */
    mouse->stop();raw->stop();
    assert(!dlclose(mouse_module)&&!dlclose(raw_module));
    puts("USB mouse + actual raw HID ELF: empty/read/detach/scan lifecycle and lower-host quarantine boundary: PASS");
    return 0;
}
