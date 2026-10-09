#include "RiscProviderHealthV1.h"
#include "RiscUsbHidV1.h"
#include "RiscUsbMouseV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
static unsigned calls,closes;static bool fail=true;static unsigned kind;
static const uint8_t pad_descriptor[]={0x05,1,0x09,5,0xa1,1,0x05,9,0x19,1,0x29,2,0x15,0,0x25,1,0x75,1,0x95,2,0x81,2,0x75,6,0x95,1,0x81,3,0x05,1,0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7f,0x75,8,0x95,2,0x81,2,0xc0};
static bool scan(void*c,size_t n){(void)c;(void)n;++calls;return true;}
static bool items(void*c,risc_usb_hid_interface_v1*out,size_t*n){(void)c;++calls;assert(*n);*n=1;*out=(risc_usb_hid_interface_v1){.device=51,.subclass=kind==0||kind==2?1:0,.protocol=kind==0?1:kind==2?2:0,.interrupt_in=0x81,.max_packet=8,.report_descriptor_length=sizeof(pad_descriptor)};return true;}
static uint64_t open_hid(void*c,uint64_t d,uint8_t i,uint8_t a){(void)c;(void)i;(void)a;++calls;assert(d==51);return 91;}
static bool desc(void*c,uint64_t s,uint8_t*out,size_t*n){(void)c;(void)s;++calls;assert(*n>=sizeof(pad_descriptor));memcpy(out,pad_descriptor,sizeof(pad_descriptor));*n=sizeof(pad_descriptor);return true;}
static bool boot(void*c,uint64_t s,bool b){(void)c;(void)s;(void)b;++calls;return true;}
static int32_t report(void*c,uint64_t s,uint8_t*out,size_t n,uint32_t ms){(void)c;(void)s;(void)out;(void)n;(void)ms;++calls;return 0;}
static bool present(void*c,uint64_t s){(void)c;(void)s;++calls;return true;}
static bool close_hid(void*c,uint64_t s){assert(c==(void*)0x1234&&s==91);++calls;++closes;return !fail;}
static uint64_t now(void*c){(void)c;++calls;return 100;}
static void sleep_ms(void*c,uint32_t ms){(void)c;(void)ms;++calls;}
static const risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),0,now,sleep_ms};
static uint64_t sub(void*c,uint64_t f){(void)c;(void)f;++calls;return 91;}
static bool unsub(void*c,uint64_t s){return close_hid(c,s);}
static bool poll_key(void*c,size_t n){(void)c;(void)n;++calls;return true;}
static int32_t next_key(void*c,uint64_t s,risc_usb_keyboard_event_v1*out){(void)c;(void)s;(void)out;++calls;return 0;}
static bool snapshot_key(void*c,risc_usb_keyboard_state_v1*out,size_t*n){(void)c;(void)out;++calls;*n=0;return true;}
static const risc_usb_keyboard_api_v1 keyboard_api={1,sizeof(keyboard_api),(void*)0x1234,sub,unsub,poll_key,next_key,snapshot_key};
static const risc_usb_hid_api_v1 hid_api={1,sizeof(hid_api),(void*)0x1234,scan,items,open_hid,desc,boot,report,present,close_hid};
int main(int argc,char**argv){assert(argc==3);kind=!strcmp(argv[2],"keyboard")?0:!strcmp(argv[2],"gamepad")?1:!strcmp(argv[2],"mouse")?2:3;
 void*module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module){puts(dlerror());assert(0);}risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(module,"t5_driver_get");assert(get);const risc_driver_v2*d=get(2);const risc_provider_health_v1*h=dlsym(module,"risc_provider_health_v1_descriptor");assert(h);
 risc_provider_dependency_v1 deps[]={{kind==3?"usb.hid.keyboard":"usb.hid",1,kind==3?(const void*)&keyboard_api:(const void*)&hid_api},{"platform.clock",1,&clock_api}};
 size_t count=kind==0||kind==3?1:2;assert(d->start(deps,count));unsigned before=calls;assert(!d->start(deps,count)&&calls==before);assert(h->check()==RISC_PROVIDER_HEALTH_READY&&calls==before);
 if(kind==0)assert(((const risc_usb_keyboard_api_v1*)d->capability)->poll(0,1));
 if(kind==1)assert(((const risc_usb_gamepad_api_v1*)d->capability)->poll(0,1));
 if(kind==2)assert(((const risc_usb_mouse_api_v1*)d->capability)->poll(0,1));
 assert(!d->quiesce()&&closes==1);before=calls;for(unsigned i=0;i<4;++i)assert(h->check()==RISC_PROVIDER_HEALTH_RETAINED);assert(calls==before);
 /* This separate compatibility fixture deliberately permits the old retry
  * before any Runtime transition. It proves successful legacy cleanup cannot
  * erase the health profile's terminal observation. Real Runtime never retries
  * a mapping after observing RETAINED. */
 fail=false;assert(d->quiesce());d->stop();before=calls;assert(h->check()==RISC_PROVIDER_HEALTH_RETAINED&&calls==before);assert(!dlclose(module));
 puts("Actual semantic dependency facade: original context, no active-start overwrite, false cleanup sticky despite legacy retry PASS");}
