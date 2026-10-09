/* Exercise production provider and real TinyUSB control/BOT/SCSI state machine
 * with a fake DCD packet transport and independently checked storage leases. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tusb.h"
#include "device/dcd.h"
#include "RiscUsbDeviceMscV1.h"
#include "RiscUsbPhyResourceV1.h"
#include "RiscStorageExportV1.h"
#include "RiscProviderV2.h"
#include "RiscPlatformClockV1.h"
#include "Transport.h"
extern const risc_driver_v2 *t5_driver_get(uint32_t);
static bool is_owner=true,sd_owned,phy_owned,usb_live,healthy=true;
static bool fail_claim,retain_claim,fail_start,fail_stop,fail_release,fail_sync,fail_end,fail_write,refuse_begin;
static bool absent_end,retained_begin,no_media_token,no_phy_token;
static uint64_t media_generation=1;
static unsigned sd_reads,sd_writes,sd_syncs,remounts;
static uint8_t disk[16][512];
static char trace[128];static unsigned trace_size;
static void mark(char c){assert(trace_size+1<sizeof(trace));trace[trace_size++]=c;trace[trace_size]=0;}
static void sd_check(uint64_t t){assert(is_owner && sd_owned && t==media_generation);}
static int32_t sd_begin(void*c,uint64_t*t,uint64_t*n,uint32_t*z){
 (void)c;*t=0;*n=0;*z=0;if(retained_begin)return -2;if(refuse_begin)return -1;assert(!sd_owned);sd_owned=true;mark('M');*t=no_media_token?0:++media_generation;*n=16;*z=512;return 0;
}
static int32_t sd_read(void*c,uint64_t t,uint64_t lba,uint32_t n,void*b){
 (void)c;sd_check(t);assert(usb_live && lba<16 && n==1);++sd_reads;memcpy(b,disk[lba],512);return 0;
}
static int32_t sd_write(void*c,uint64_t t,uint64_t lba,uint32_t n,const void*b){
 (void)c;sd_check(t);assert(usb_live && lba<16 && n==1);++sd_writes;if(fail_write)return -2;memcpy(disk[lba],b,512);return 0;
}
static int32_t sd_sync(void*c,uint64_t t){(void)c;sd_check(t);++sd_syncs;return fail_sync?-2:0;}
static int32_t sd_end(void*c,uint64_t t){(void)c;sd_check(t);assert(!usb_live);mark('E');if(fail_end)return -2;sd_owned=false;++remounts;return absent_end?1:0;}
static bool yes(void*c){(void)c;return true;}
static int32_t zero(void*c){(void)c;return 0;}
static bool owner(void*c){(void)c;return is_owner;}
static bool claim(void*c,uint64_t*t){(void)c;assert(sd_owned && !phy_owned);*t=0;mark('P');if(fail_claim&&!retain_claim)return false;phy_owned=true;*t=no_phy_token?0:9;return !fail_claim;}
static bool release(void*c,uint64_t t){(void)c;assert(!sd_owned && !usb_live && phy_owned && t==9);mark('R');if(fail_release)return false;phy_owned=false;return true;}
static uint64_t now(void*c){(void)c;return 0;}
static void sleep_ms(void*c,uint32_t ms){(void)c;assert(phy_owned && sd_owned && ms==30);}
static risc_storage_volume_api_v1_export volume={
 .sleep={.terminal={.power={.volume={.base={.api_version=1,.struct_size=sizeof(volume)}},.prepare_power_down=yes,.cancel_power_down=yes},.extension_tag=RISC_STORAGE_POWER_COMMIT_TAG,.extension_version=1,.commit_power_down=yes},.sleep_tag=RISC_STORAGE_SLEEP_TAG,.sleep_version=1,.prepare_sleep=yes,.commit_sleep=yes,.resume_sleep=zero},
 .export_tag=RISC_STORAGE_EXPORT_TAG,.export_version=1,.export_begin=sd_begin,.export_read=sd_read,.export_write=sd_write,.export_sync=sd_sync,.export_end=sd_end
};
static risc_usb_phy_resource_api_v1 phy={1,sizeof(phy),NULL,RISC_USB_PHY_ESP32S3_OTG,0,owner,claim,release};
static const risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,now,sleep_ms};
static struct {uint8_t *buffer;uint16_t length;bool pending,stalled;} ep[2][2];
static unsigned slot(uint8_t addr){assert((addr&0x7f)<2);return addr&0x7f;}
void dcd_init(uint8_t p){(void)p;memset(ep,0,sizeof(ep));}
void dcd_int_enable(uint8_t p){(void)p;}
void dcd_int_disable(uint8_t p){(void)p;}
void dcd_connect(uint8_t p){(void)p;}
void dcd_disconnect(uint8_t p){(void)p;}
void dcd_remote_wakeup(uint8_t p){(void)p;assert(false);}
void dcd_sof_enable(uint8_t p,bool x){(void)p;(void)x;}
void dcd_set_address(uint8_t p,uint8_t a){(void)a;assert(dcd_edpt_xfer(p,0x80,NULL,0));}
bool dcd_edpt_open(uint8_t p,const tusb_desc_endpoint_t*d){(void)p;assert(d->bEndpointAddress==1||d->bEndpointAddress==0x81);return true;}
void dcd_edpt_close_all(uint8_t p){(void)p;memset(&ep[1],0,sizeof(ep[1]));}
void dcd_edpt_close(uint8_t p,uint8_t a){(void)p;memset(&ep[slot(a)][a>>7],0,sizeof(ep[0][0]));}
bool dcd_edpt_xfer(uint8_t p,uint8_t a,uint8_t*b,uint16_t n){
 (void)p;unsigned e=slot(a),d=a>>7;assert(!ep[e][d].pending);ep[e][d].buffer=b;ep[e][d].length=n;ep[e][d].pending=true;return true;
}
void dcd_edpt_stall(uint8_t p,uint8_t a){(void)p;ep[slot(a)][a>>7].stalled=true;ep[slot(a)][a>>7].pending=false;}
void dcd_edpt_clear_stall(uint8_t p,uint8_t a){(void)p;ep[slot(a)][a>>7].stalled=false;}
bool risc_msc_transport_start(void){assert(sd_owned&&phy_owned);mark('U');usb_live=true;return !fail_start&&tud_init(0);}
bool risc_msc_transport_poll(void){assert(usb_live);tud_task_ext(0,false);return healthy;}
bool risc_msc_transport_stop(void){assert(usb_live);mark('T');if(fail_stop)return false;usb_live=false;risc_msc_stack_reset();return true;}
void risc_msc_transport_fault(void){healthy=false;}
bool risc_msc_transport_ok(void){return healthy;}
static const risc_usb_device_msc_api_v1 *api;static uint64_t token;
static risc_usb_device_msc_status_v1 status;
static int32_t poll(void){status.struct_size=sizeof(status);return api->poll(NULL,token,&status);}
static void complete(uint8_t a,const void*input,uint32_t count){
 unsigned e=slot(a),d=a>>7;assert(ep[e][d].pending&&count<=ep[e][d].length);
 if(!d&&count)memcpy(ep[e][d].buffer,input,count);
 ep[e][d].pending=false;dcd_event_xfer_complete(0,a,count,XFER_RESULT_SUCCESS,false);
}
static void setup(uint8_t request,uint16_t value){
 uint8_t bytes[8]={0,request,(uint8_t)value,(uint8_t)(value>>8),0,0,0,0};
 dcd_event_setup_received(0,bytes,false);assert(poll()==0);assert(ep[0][1].pending&&ep[0][1].length==0);
 complete(0x80,NULL,0);assert(poll()==0);
}
static void configure(void){dcd_event_bus_reset(0,TUSB_SPEED_FULL,false);assert(poll()==0);setup(TUSB_REQ_SET_ADDRESS,1);setup(TUSB_REQ_SET_CONFIGURATION,1);assert(status.state==RISC_USB_MSC_CONNECTED);assert(ep[1][0].pending&&ep[1][0].length==31);}
static uint32_t tag;
static void command(uint8_t op,uint32_t bytes,bool input,uint32_t lba,uint16_t count,uint8_t control){
 msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=++tag,.total_bytes=bytes,.dir=input?0x80:0,.lun=0,.cmd_len=(op==0x28||op==0x2a||op==0x25||op==0x35)?10:6};
 cbw.command[0]=op;
 if(op==0x28||op==0x2a){cbw.command[2]=(uint8_t)(lba>>24);cbw.command[3]=(uint8_t)(lba>>16);cbw.command[4]=(uint8_t)(lba>>8);cbw.command[5]=(uint8_t)lba;cbw.command[7]=(uint8_t)(count>>8);cbw.command[8]=(uint8_t)count;}
 else cbw.command[4]=control;
 complete(1,&cbw,sizeof(cbw));(void)poll();
}
static unsigned csw(bool finish){
 assert(ep[1][1].pending&&ep[1][1].length==13);msc_csw_t c;memcpy(&c,ep[1][1].buffer,sizeof(c));assert(c.signature==MSC_CSW_SIGNATURE&&c.tag==tag);
 if(finish){complete(0x81,NULL,13);(void)poll();}return c.status;
}
static void connect_provider(void){
 const risc_driver_v2*d=t5_driver_get(2);assert(d&&!t5_driver_get(1));api=d->capability;
 risc_provider_dependency_v1 deps[]={{"platform.clock",1,&clock_api},{RISC_USB_PHY_RESOURCE_CAPABILITY,1,&phy},{"storage.volume",1,&volume}};
 assert(d->start(deps,3));assert(api->begin(NULL,&token)==0&&token);assert(strcmp(trace,"MPU")==0);
}
int main(int argc,char**argv){
 assert(argc==2);const char*s=argv[1];
 const risc_driver_v2*d=t5_driver_get(2);
 if(!strcmp(s,"begin-refused")){
  refuse_begin=true;api=d->capability;risc_provider_dependency_v1 deps[]={{"platform.clock",1,&clock_api},{RISC_USB_PHY_RESOURCE_CAPABILITY,1,&phy},{"storage.volume",1,&volume}};assert(d->start(deps,3));assert(api->begin(NULL,&token)==-1&&!token&&!trace_size);assert(d->quiesce());return 0;
 }
 if(!strcmp(s,"begin-retained-zero")||!strcmp(s,"bad-sd-token")||!strcmp(s,"bad-phy-token")) {
  retained_begin=!strcmp(s,"begin-retained-zero");no_media_token=!strcmp(s,"bad-sd-token");no_phy_token=!strcmp(s,"bad-phy-token");api=d->capability;
  risc_provider_dependency_v1 deps[]={{"platform.clock",1,&clock_api},{RISC_USB_PHY_RESOURCE_CAPABILITY,1,&phy},{"storage.volume",1,&volume}};assert(d->start(deps,3));
  assert(api->begin(NULL,&token)==-2&&token);assert(poll()==-2);assert(api->end(NULL,token,1)==-2&&!usb_live&&!remounts);assert(!d->quiesce());return 0;
 }
 if(!strcmp(s,"phy-refused")||!strcmp(s,"start-failed")||!strcmp(s,"phy-retained")){
  fail_claim=strncmp(s,"phy-",4)==0;retain_claim=!strcmp(s,"phy-retained");fail_start=!strcmp(s,"start-failed");api=d->capability;
  risc_provider_dependency_v1 deps[]={{"platform.clock",1,&clock_api},{RISC_USB_PHY_RESOURCE_CAPABILITY,1,&phy},{"storage.volume",1,&volume}};assert(d->start(deps,3));int r=api->begin(NULL,&token);
  if(retain_claim){assert(r==-2&&token&&sd_owned&&phy_owned&&!d->quiesce());assert(api->end(NULL,token,0)==0);}
  else {assert(r<0&&!token&&!sd_owned&&!phy_owned);assert(strcmp(trace,fail_start?"MPUTER":"MPE")==0);}
  assert(d->quiesce());return 0;
 }
 connect_provider();assert(!d->quiesce());
 if(!strcmp(s,"nonowner")){is_owner=false;assert(poll()==-1);assert(api->end(NULL,token,0)==-1);is_owner=true;assert(api->end(NULL,token,0)==0);assert(d->quiesce());return 0;}
 if(!strcmp(s,"cancel-waiting")){assert(api->end(NULL,token,0)==0);assert(strcmp(trace,"MPUTER")==0);assert(poll()==-1);assert(d->quiesce());return 0;}
 configure();
 if(!strcmp(s,"suspend")){dcd_event_bus_signal(0,DCD_EVENT_SUSPEND,false);assert(poll()==0&&status.state==RISC_USB_MSC_SUSPENDED);assert(sd_owned&&phy_owned&&!remounts);assert(api->end(NULL,token,0)==-1);dcd_event_bus_signal(0,DCD_EVENT_RESUME,false);assert(poll()==0&&status.state==RISC_USB_MSC_CONNECTED);}
 else if(!strcmp(s,"unconfigure")){setup(TUSB_REQ_SET_CONFIGURATION,0);assert(sd_owned&&!remounts&&status.state==RISC_USB_MSC_WAITING);assert(api->end(NULL,token,0)==-1);}
 else if(!strcmp(s,"disconnect")){dcd_event_bus_signal(0,DCD_EVENT_UNPLUGGED,false);assert(poll()==0&&status.state==RISC_USB_MSC_DISCONNECTED);assert(!sd_owned&&!phy_owned&&remounts==1);}
 else if(!strcmp(s,"read-write")){
  command(0x25,8,true,0,0,0);assert(ep[1][1].length==8);assert(ep[1][1].buffer[3]==15&&ep[1][1].buffer[6]==2);complete(0x81,NULL,8);assert(poll()==0);assert(csw(true)==0);
  uint8_t b[512];memset(b,0xa7,512);command(0x2a,512,false,3,1,0);assert(ep[1][0].length==512);complete(1,b,512);assert(poll()==0);assert(csw(true)==0&&sd_writes==1&&!memcmp(disk[3],b,512));
  command(0x28,512,true,3,1,0);assert(ep[1][1].length==512&&!memcmp(ep[1][1].buffer,b,512));complete(0x81,NULL,512);assert(poll()==0);assert(csw(true)==0&&sd_reads==1);
  command(0x35,0,false,0,0,0);assert(csw(true)==0&&sd_syncs==1);
 }
 else if(!strcmp(s,"eject")||!strcmp(s,"eject-no-fs")){
  absent_end=!strcmp(s,"eject-no-fs");command(0x1b,0,false,0,0,2);assert(csw(false)==0&&sd_owned&&phy_owned&&!remounts);
  complete(0x81,NULL,13);assert(poll()==0);assert(status.state==(absent_end?RISC_USB_MSC_MEDIA_UNAVAILABLE:RISC_USB_MSC_EJECTED));assert(!sd_owned&&!phy_owned&&remounts==1&&sd_syncs==1);
 }
 else if(!strcmp(s,"prevent-eject")){command(0x1e,0,false,0,0,1);assert(csw(true)==0);command(0x1b,0,false,0,0,2);assert(csw(true)==1&&sd_owned&&!remounts);}
 else if(!strcmp(s,"wrong-sector-size")){command(0x2a,1024,false,0,1,0);assert(ep[1][0].stalled&&!sd_writes);}
 else if(!strcmp(s,"lba-past-capacity")){command(0x2a,1024,false,15,2,0);assert(ep[1][0].stalled&&!sd_writes);}
 else if(!strcmp(s,"invalid-cbw")){
  msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.lun=1,.cmd_len=17};complete(1,&cbw,sizeof(cbw));assert(poll()==0);assert(ep[1][0].stalled&&ep[1][1].stalled&&!sd_writes&&!sd_reads);
 }
 else if(!strcmp(s,"repeat")){
  uint64_t old=token;assert(api->end(NULL,token,1)==0);assert(api->begin(NULL,&token)==0&&token!=old);assert(api->end(NULL,old,1)==-1);configure();
 }
 else if(!strcmp(s,"eject-sync-retained")){
  fail_sync=fail_end=true;command(0x1b,0,false,0,0,2);assert(poll()==-2&&sd_owned&&!remounts);assert(api->end(NULL,token,1)==-2&&sd_owned&&phy_owned);assert(!d->quiesce());return 0;
 }
 else if(!strcmp(s,"lba-overflow")){command(0x28,1024,true,UINT32_MAX,2,0);assert(ep[1][1].stalled&&!sd_reads);}
 else if(!strcmp(s,"write-retained")){
  fail_write=true;fail_end=true;uint8_t b[512]={0};command(0x2a,512,false,0,1,0);complete(1,b,512);assert(poll()==-2&&status.state==RISC_USB_MSC_FAULT_RETAINED);assert(sd_writes==1&&sd_owned&&!remounts);
  assert(api->end(NULL,token,1)==-2&&sd_owned&&phy_owned&&!usb_live);assert(!d->quiesce());puts("PASS retained write");return 0;
 }
 else if(!strcmp(s,"stop-retained")){fail_stop=true;assert(api->end(NULL,token,1)==-2&&usb_live&&sd_owned&&phy_owned&&!remounts);assert(!d->quiesce());return 0;}
 else if(!strcmp(s,"remount-retained")){fail_end=true;assert(api->end(NULL,token,1)==-2&&!usb_live&&sd_owned&&phy_owned&&!remounts);assert(!d->quiesce());return 0;}
 else if(!strcmp(s,"phy-release-retained")){fail_release=true;assert(api->end(NULL,token,1)==-2&&!usb_live&&!sd_owned&&phy_owned&&remounts==1);assert(!d->quiesce());fail_release=false;}
 else if(!strcmp(s,"queue-overflow")){for(unsigned i=0;i<40;++i)dcd_event_bus_signal(0,DCD_EVENT_RESUME,false);assert(!healthy&&poll()==-2&&sd_owned&&!remounts);}
 else if(!strcmp(s,"stale")){assert(api->end(NULL,token+1,1)==-1&&sd_owned);}
 else assert(!"unknown scenario");
 assert(api->end(NULL,token,1)==0&&!sd_owned&&!phy_owned&&!usb_live);assert(poll()==-1);assert(d->quiesce());printf("PASS %s\n",s);
}
