/* Real host, raw HID and every selected semantic ELF. Only the hardware and
 * clock boundary is modeled. No test-only health descriptor is substituted. */
#include "RiscProviderHealthV1.h"
#include "RiscUsbInterruptV1.h"
#include "RiscUsbHidV1.h"
#include "RiscUsbMouseV1.h"
#include "RiscTextInputV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
static const uint8_t pad_desc[]={0x05,1,0x09,5,0xa1,1,0x05,9,0x19,1,0x29,2,0x15,0,0x25,1,0x75,1,0x95,2,0x81,2,0x75,6,0x95,1,0x81,3,0x05,1,0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,2,0xc0};
static const uint8_t config[]={9,2,100,0,4,1,0,0x80,50,
 9,4,0,0,1,3,1,1,0,9,0x21,0x11,1,0,1,0x22,63,0,7,5,0x81,3,8,0,10,
 9,4,1,0,1,3,1,2,0,9,0x21,0x11,1,0,1,0x22,50,0,7,5,0x82,3,8,0,10,
 9,4,2,0,1,3,0,0,0,9,0x21,0x11,1,0,1,0x22,sizeof(pad_desc),0,7,5,0x83,3,8,0,10,
 9,4,3,0,1,0xff,0x5d,1,0,7,5,0x84,3,32,0,10};
static unsigned calls,releases,claim_calls,queue_event,ready[4];
static bool claimed[4],fail_release;
static uint64_t now=100;
static int32_t event(void*c,risc_usb_controller_event_v1*out){(void)c;++calls;if(!queue_event)return 0;*out=(risc_usb_controller_event_v1){queue_event,51};queue_event=0;return 1;}
static bool configuration(void*c,uint64_t d,uint8_t*out,size_t*n,uint16_t*v,uint16_t*p){(void)c;++calls;assert(d==51&&*n>=sizeof(config));memcpy(out,config,sizeof(config));*n=sizeof(config);*v=0x1234;*p=0x5678;return true;}
static bool claim(void*c,uint64_t d,uint8_t i,uint8_t a,uint64_t*out){(void)c;++calls;assert(d==51&&i<4&&!a&&!claimed[i]);claimed[i]=true;*out=100+i;++claim_calls;return true;}
static bool release(void*c,uint64_t t){(void)c;++calls;++releases;assert(t>=100&&t<104&&claimed[t-100]);if(fail_release)return false;claimed[t-100]=false;return true;}
static int32_t control(void*c,uint64_t d,uint8_t type,uint8_t req,uint16_t val,uint16_t i,uint8_t*out,uint16_t n,uint32_t ms){(void)c;++calls;assert(d==51&&i<4&&claimed[i]&&ms);if(type==0x21&&req==0x0b&&!val&&!n)return 0;if(type==0x81&&req==6&&val==0x2200&&i==2&&n==sizeof(pad_desc)){memcpy(out,pad_desc,n);return n;}return -1;}
static int32_t read_bulk(void*c,uint64_t t,uint8_t e,uint8_t*b,size_t n,uint32_t m){(void)c;(void)t;(void)e;(void)b;(void)n;(void)m;++calls;return -1;}
static int32_t write_bulk(void*c,uint64_t t,uint8_t e,const uint8_t*b,size_t n,uint32_t m){(void)c;(void)t;(void)e;(void)b;(void)n;(void)m;++calls;return -1;}
static bool quiesce(void*c){(void)c;++calls;for(unsigned i=0;i<4;++i)if(claimed[i])return false;return true;}
static int32_t interrupt(void*c,uint64_t t,uint8_t e,uint8_t*out,size_t n,uint32_t ms){(void)c;++calls;assert(t>=100&&t<104&&e==0x81+t-100&&claimed[t-100]&&n>=8&&ms);unsigned i=t-100;if(!ready[i])return 0;--ready[i];memset(out,0,n);if(!i){out[2]=4;return 8;}if(i==1){out[0]=1;out[1]=2;out[2]=3;return 3;}if(i==2){out[0]=1;out[1]=2;out[2]=3;return 3;}out[1]=20;out[3]=0x10;return 20;}
static const risc_usb_controller_interrupt_v1 controller={{1,sizeof(controller),0,event,configuration,claim,release,control,read_bulk,write_bulk,quiesce},interrupt};
static uint64_t clock_now(void*c){(void)c;++calls;return now;}
static void clock_sleep(void*c,uint32_t ms){(void)c;++calls;now+=ms;}
static const risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),0,clock_now,clock_sleep};
static const char *names[]={"usb_host_v2","usb_hid","usb_hid_keyboard","usb_hid_mouse","usb_hid_gamepad","usb_xinput_gamepad","usb_hid_text_input"};
static void *modules[7];
static const risc_driver_v2 *drivers[7];
static const risc_provider_health_v1 *health[7];
static void check(unsigned i,int32_t expected){unsigned before=calls;for(unsigned n=0;n<3;++n)assert(health[i]->check()==expected);assert(calls==before);}
int main(int argc,char**argv){
 assert(argc==3);bool retained=!strcmp(argv[2],"retained");
 for(unsigned i=0;i<7;++i){char path[1024];snprintf(path,sizeof(path),"%s/%s.so",argv[1],names[i]);modules[i]=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!modules[i]){puts(dlerror());assert(0);}risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(modules[i],"t5_driver_get");assert(get);drivers[i]=get(2);health[i]=dlsym(modules[i],"risc_provider_health_v1_descriptor");assert(health[i]&&health[i]->api_version==1&&health[i]->struct_size==sizeof(*health[i]));check(i,RISC_PROVIDER_HEALTH_READY);}
 risc_provider_dependency_v1 hd={"usb.controller",1,&controller};assert(drivers[0]->start(&hd,1));
 risc_provider_dependency_v1 rd={"usb.host",1,drivers[0]->capability};assert(drivers[1]->start(&rd,1));
 risc_provider_dependency_v1 kd={"usb.hid",1,drivers[1]->capability};assert(drivers[2]->start(&kd,1));
 risc_provider_dependency_v1 cd[]={{"usb.hid",1,drivers[1]->capability},{"platform.clock",1,&clock_api}};
 assert(drivers[3]->start(cd,2)&&drivers[4]->start(cd,2));cd[0]=rd;assert(drivers[5]->start(cd,2));
 risc_provider_dependency_v1 td={"usb.hid.keyboard",1,drivers[2]->capability};assert(drivers[6]->start(&td,1));
 const risc_usb_keyboard_api_v1*k=drivers[2]->capability;const risc_usb_mouse_api_v1*m=drivers[3]->capability;const risc_usb_gamepad_api_v1*g=drivers[4]->capability,*x=drivers[5]->capability;const risc_text_input_api_v1*t=drivers[6]->capability;
 uint64_t ks=k->subscribe(0,0),ms=m->subscribe(0,0),gs=g->subscribe(0,0),xs=x->subscribe(0,0),ts=t->subscribe(0,0);assert(ks&&ms&&gs&&xs&&ts);size_t baseline_count=0;assert(m->snapshot(0,ms,0,&baseline_count)&&!baseline_count);
 for(unsigned i=0;i<7;++i)check(i,RISC_PROVIDER_HEALTH_READY);
 queue_event=1;for(unsigned i=0;i<4;++i)ready[i]=1;
 assert(t->poll(0,4)&&m->poll(0,4)&&g->poll(0,4)&&x->poll(0,4));assert(claim_calls==4);
 risc_usb_keyboard_event_v1 key;assert(k->next(0,ks,&key)==1&&key.kind==1);assert(k->next(0,ks,&key)==1&&key.usage==4);
 risc_usb_mouse_event_v1 mouse;assert(m->next(0,ms,&mouse)==1&&mouse.kind==1);assert(m->next(0,ms,&mouse)==1&&mouse.x==2&&mouse.y==3);
 risc_usb_gamepad_state_v1 pad;size_t n=1;assert(g->snapshot(0,&pad,&n)&&n==1&&pad.buttons==1);n=1;assert(x->snapshot(0,&pad,&n)&&n==1&&pad.connected);
 risc_text_input_event_v1 text;assert(t->next(0,ts,&text)==1&&text.kind==RISC_TEXT_EVENT_CONNECTED);assert(t->next(0,ts,&text)==1&&text.codepoint=='a');
 for(unsigned i=0;i<7;++i){check(i,RISC_PROVIDER_HEALTH_READY);if(i>=2)assert(!drivers[i]->quiesce());}
 if(retained){fail_release=true;queue_event=2;assert(m->poll(0,1));check(3,RISC_PROVIDER_HEALTH_READY);check(1,RISC_PROVIDER_HEALTH_READY);check(0,RISC_PROVIDER_HEALTH_RETAINED);assert(releases==1&&claimed[1]);puts("Real host/raw HID/mouse: semantic close success hides exact failed physical claim; terminal health, no observer I/O PASS");return 0;}
 assert(t->unsubscribe(0,ts)&&m->unsubscribe(0,ms)&&g->unsubscribe(0,gs)&&x->unsubscribe(0,xs)&&k->unsubscribe(0,ks));
 for(int i=6;i>=0;--i){assert(drivers[i]->quiesce());drivers[i]->stop();check(i,RISC_PROVIDER_HEALTH_READY);assert(dlclose(modules[i])==0);}
 assert(releases==4);puts("Real host/HID keyboard/mouse/gamepad/XInput/text: live copied state READY, subscriptions, normal quiesce and readonly checks PASS");return 0;
}
