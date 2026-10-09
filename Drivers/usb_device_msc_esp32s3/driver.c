/* Shared USB-device MSC owner. TinyUSB handles BOT/SCSI packet framing; this
 * ordinary ELF owns the stack, internal-PHY lease and exclusive SD export. */
#include <RiscProviderV2.h>
#include <RiscPlatformClockV1.h>
#include <RiscUsbPhyResourceV1.h>
#include <RiscUsbDeviceMscV1.h>
#include <RiscStorageExportV1.h>
#include <string.h>
#include "Transport.h"
#include "tusb.h"
#include "device/dcd.h"
static const risc_platform_clock_api_v1 *clock_api;
static const risc_usb_phy_resource_api_v1 *phy;
static const risc_storage_volume_api_v1_export *volume;
static const risc_storage_volume_api_v1_export_prepare *preparation;
static char storage_detail[240];
static uint64_t phy_token,media_token,session,next_session;
static uint64_t blocks,reads,writes;
static uint32_t state=RISC_USB_MSC_IDLE;
static bool started,busy,poisoned,transport_live,ever_configured,eject_requested,eject_complete,unplugged;
static risc_usb_device_msc_diagnostics_v1 diagnostic;
static uint64_t previous_poll_ms;
static bool polled,command_active,media_faulted;
static bool media_ready,prevent_removal,media_unknown,phy_unknown,faulted;
static const char *error;
uint64_t risc_msc_now(void) { return clock_api?clock_api->monotonic_ms(clock_api->context):0; }
static uint64_t elapsed(uint64_t now,uint64_t before) { return now>=before?now-before:0; }
void risc_msc_command_started(uint8_t opcode,uint32_t tag,uint32_t bytes,uint32_t lba,uint32_t count) {
 ++diagnostic.commands_started;diagnostic.current_opcode=opcode;diagnostic.current_tag=tag;
 diagnostic.command_bytes=bytes;diagnostic.current_lba=lba;diagnostic.current_block_count=count;
 diagnostic.command_started_ms=risc_msc_now();command_active=true;
}
void risc_msc_command_completed(uint8_t status) {
 ++diagnostic.commands_completed;diagnostic.completed_opcode=diagnostic.current_opcode;
 diagnostic.completed_tag=diagnostic.current_tag;diagnostic.last_csw_status=status;
 diagnostic.last_command_elapsed_ms=elapsed(risc_msc_now(),diagnostic.command_started_ms);command_active=false;
}
void risc_msc_protocol_stall(void) { ++diagnostic.stalls; }
void risc_msc_pump_report(uint32_t passes) { diagnostic.last_pump_passes=passes; }
static void record_io(uint32_t lba,uint32_t count,uint64_t before,int32_t result) {
 diagnostic.last_io_lba=lba;diagnostic.last_io_count=count;diagnostic.last_io_result=result;
 diagnostic.last_io_elapsed_ms=elapsed(risc_msc_now(),before);
}
static bool owner(void) { return phy && phy->is_owner(phy->context); }
static bool enter(void) {
 if(!owner() || busy || poisoned)return false;
 busy=true;return true;
}
static bool leave(void) {
 if(!owner()){poisoned=faulted=true;state=RISC_USB_MSC_FAULT_RETAINED;risc_msc_transport_fault();return false;}
 busy=false;return true;
}
static int32_t finish(int32_t result) { return leave()?result:RISC_USB_MSC_RETAINED; }
static void fault(const char *why) { error=why;faulted=true;state=RISC_USB_MSC_FAULT_RETAINED;risc_msc_transport_fault(); }
static bool cleanup(void) {
 if(media_unknown || phy_unknown){fault("Ownership has no safe token; restart required");return false;}
 /* No SD remount or native console reclaim until no callback, endpoint, DMA or
  * queued transport work can still access the media token. Cooperative polling
  * means this call cannot race any callback. Stop resets hardware, then queue. */
 if(transport_live) {
  if(!risc_msc_transport_stop()){fault("USB shutdown retained; SD remains exported");return false;}
  transport_live=false;
 }
 if(media_token) {
  const int32_t result=volume->export_end(volume->sleep.terminal.power.volume.base.context,media_token);
  if(result!=RISC_STORAGE_EXPORT_READY && result!=RISC_STORAGE_EXPORT_MEDIA_UNAVAILABLE){fault("SD remount retained; restart required");return false;}
  media_token=0;media_ready=result==RISC_STORAGE_EXPORT_READY;
 }
 if(phy_token) {
  if(!phy->release(phy->context,phy_token)){fault("USB PHY recovery retained");return false;}
  phy_token=0;
 }
 return true;
}
static void storage_error(int32_t result) {
 const char *prefix="SD export result=";size_t n=0;
 while(prefix[n]){storage_detail[n]=prefix[n];++n;}
 uint32_t magnitude=result<0?0u-(uint32_t)result:(uint32_t)result;
 if(result<0)storage_detail[n++]='-';
 char digits[10];unsigned count=0;do{digits[count++]=(char)('0'+magnitude%10u);magnitude/=10u;}while(magnitude);
 while(count)storage_detail[n++]=digits[--count];
 storage_detail[n++]=':';storage_detail[n++]=' ';storage_detail[n]=0;
 const risc_storage_volume_api_v1 *v=&volume->sleep.terminal.power.volume.base;
 if(v->last_error)v->last_error(v->context,storage_detail+n,sizeof(storage_detail)-n);
 storage_detail[sizeof(storage_detail)-1]=0;error=storage_detail;
}
static int32_t activate(uint32_t size) {
 if(!blocks || blocks>UINT32_MAX || size!=512u) {
  error="SD geometry unsupported by USB READ CAPACITY(10)";
  if(!cleanup())return RISC_USB_MSC_RETAINED;
  state=RISC_USB_MSC_MEDIA_UNAVAILABLE;return RISC_USB_MSC_OK;
 }
 const bool claimed=phy->claim(phy->context,&phy_token);
 if(claimed && !phy_token){phy_unknown=true;fault("USB PHY returned invalid ownership token");return RISC_USB_MSC_RETAINED;}
 if(!claimed) {
  error="USB PHY is unavailable";
  if(phy_token){fault("USB PHY claim retained");return RISC_USB_MSC_RETAINED;}
  if(!cleanup())return RISC_USB_MSC_RETAINED;
  state=RISC_USB_MSC_MEDIA_UNAVAILABLE;return RISC_USB_MSC_OK;
 }
 ever_configured=eject_requested=eject_complete=unplugged=prevent_removal=false;
 /* Logical disconnect interval also covers a computer attached before boot. */
 clock_api->sleep_ms(clock_api->context,30);
 transport_live=true;
 if(!risc_msc_transport_start()) {
  error="USB controller start failed";
  if(!cleanup())return RISC_USB_MSC_RETAINED;
  state=RISC_USB_MSC_MEDIA_UNAVAILABLE;return RISC_USB_MSC_OK;
 }
 state=RISC_USB_MSC_WAITING;return RISC_USB_MSC_OK;
}
static int32_t begin(void *context,uint64_t *out) {
 (void)context;if(out)*out=0;
 if(!out || !enter())return RISC_USB_MSC_REFUSED;
 if(!started || session || next_session==UINT64_MAX)return finish(RISC_USB_MSC_REFUSED);
 error=NULL;reads=writes=blocks=0;media_ready=false;faulted=media_faulted=false;
 memset(&diagnostic,0,sizeof(diagnostic));polled=command_active=false;previous_poll_ms=0;
 ever_configured=eject_requested=eject_complete=unplugged=prevent_removal=false;
 const int32_t result=preparation->begin_prepare(volume->sleep.terminal.power.volume.base.context,&media_token);
 if(result!=RISC_STORAGE_EXPORT_PREPARING) {
  storage_error(result);
  if(media_token || result==RISC_STORAGE_EXPORT_RETAINED){media_unknown=!media_token;session=++next_session;*out=session;fault(error);return finish(RISC_USB_MSC_RETAINED);}
  return finish(RISC_USB_MSC_REFUSED);
 }
 session=++next_session;*out=session;
 if(!media_token){media_unknown=true;fault("SD returned invalid preparation token");return finish(RISC_USB_MSC_RETAINED);}
 state=RISC_USB_MSC_PREPARING;return finish(RISC_USB_MSC_OK);
}
static int32_t prepare_step(void *context,uint64_t token) {
 (void)context;if(!enter())return RISC_USB_MSC_REFUSED;
 if(!session || token!=session || state!=RISC_USB_MSC_PREPARING)return finish(RISC_USB_MSC_REFUSED);
 uint32_t size=0;
 const int32_t result=preparation->prepare_step(volume->sleep.terminal.power.volume.base.context,media_token,&blocks,&size);
 if(result==RISC_STORAGE_EXPORT_PREPARING)return finish(RISC_USB_MSC_OK);
 if(result==RISC_STORAGE_EXPORT_READY)return finish(activate(size));
 storage_error(result);
 if(result==RISC_STORAGE_EXPORT_RETAINED){fault(error);return finish(RISC_USB_MSC_RETAINED);}
 if(!cleanup())return finish(RISC_USB_MSC_RETAINED);
 state=RISC_USB_MSC_MEDIA_UNAVAILABLE;return finish(RISC_USB_MSC_OK);
}
static int32_t poll(void *context,uint64_t token,risc_usb_device_msc_status_v1 *out) {
 (void)context;
 if(!out || out->struct_size<sizeof(*out) || !enter())return RISC_USB_MSC_REFUSED;
 if(!session || token!=session)return finish(RISC_USB_MSC_REFUSED);
 const uint64_t now=risc_msc_now();
 diagnostic.last_poll_gap_ms=polled?elapsed(now,previous_poll_ms):0;
 if(diagnostic.last_poll_gap_ms>diagnostic.max_poll_gap_ms)diagnostic.max_poll_gap_ms=diagnostic.last_poll_gap_ms;
 previous_poll_ms=now;polled=true;diagnostic.last_pump_passes=0;
 /* Retained media may answer USB error/recovery requests, never touch SD.
  * The transport's independent hardware/queue fault latch still stops all work. */
 if(transport_live) {
  if(!risc_msc_transport_poll())fault("USB controller fault; SD custody retained");
  if(!faulted && (eject_complete || unplugged)) {
   if(cleanup())state=media_ready?(eject_complete?RISC_USB_MSC_EJECTED:RISC_USB_MSC_DISCONNECTED):RISC_USB_MSC_MEDIA_UNAVAILABLE;
  }
 }
 memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);out->state=state;
 out->blocks_read=reads;out->blocks_written=writes;out->local_media_ready=media_ready;
 return finish(state==RISC_USB_MSC_FAULT_RETAINED?RISC_USB_MSC_RETAINED:RISC_USB_MSC_OK);
}
static int32_t end(void *context,uint64_t token,uint32_t reason) {
 (void)context;
 if(reason>RISC_USB_MSC_END_CABLE_REMOVED || !enter())return RISC_USB_MSC_REFUSED;
 if(!session || token!=session)return finish(RISC_USB_MSC_REFUSED);
 if(transport_live && ever_configured && !eject_complete && !unplugged && reason!=RISC_USB_MSC_END_CABLE_REMOVED) {
  error="Eject on the computer before stopping USB storage";return finish(RISC_USB_MSC_REFUSED);
 }
 if(!cleanup())return finish(RISC_USB_MSC_RETAINED);
 session=0;state=RISC_USB_MSC_IDLE;return finish(RISC_USB_MSC_OK);
}
static int32_t diagnostics(void *context,uint64_t token,risc_usb_device_msc_diagnostics_v1 *out) {
 (void)context;if(!out || out->struct_size<sizeof(*out) || !enter())return RISC_USB_MSC_REFUSED;
 if(!session || token!=session)return finish(RISC_USB_MSC_REFUSED);
 *out=diagnostic;out->struct_size=sizeof(*out);out->blocks_read=reads;out->blocks_written=writes;
 out->flags=(ever_configured?RISC_USB_MSC_DIAG_CONFIGURED:0u) |
  (media_faulted?RISC_USB_MSC_DIAG_MEDIA_FAULT:0u) |
  (transport_live && risc_msc_transport_ok()?RISC_USB_MSC_DIAG_CONTROLLER_HEALTHY:0u) |
  (command_active?RISC_USB_MSC_DIAG_COMMAND_ACTIVE:0u);
 return finish(RISC_USB_MSC_OK);
}
static bool last_error(void *context,char *out,size_t capacity) {
 (void)context;if(!out || !capacity || !enter())return false;
 size_t n=0;if(error)while(error[n] && n+1<capacity){out[n]=error[n];++n;}out[n]=0;
 return leave() && n>0;
}
bool risc_msc_command_valid(const uint8_t *command,uint8_t length,uint32_t transfer_bytes) {
 uint8_t expected;
 switch(command[0]) {
  case 0x00:case 0x03:case 0x12:case 0x1a:case 0x1b:case 0x1e:expected=6;break;
  case 0x23:case 0x25:case 0x28:case 0x2a:case 0x35:expected=10;break;
  default:return false;
 }
 if(length!=expected)return false;
 return (command[0]!=0x00 && command[0]!=0x1b && command[0]!=0x1e && command[0]!=0x35) || !transfer_bytes;
}
bool risc_msc_command_range(uint32_t lba,uint32_t count) {
 return transport_live && risc_msc_transport_ok() && media_token && (uint64_t)lba<blocks && (uint64_t)count<=blocks-lba;
}
static bool medium(uint8_t lun) { return transport_live && lun==0 && media_token && !faulted && state!=RISC_USB_MSC_FAULT_RETAINED && !eject_requested; }
static bool media_result(int32_t result,const char *reason,uint8_t sense_code) {
 if(result==RISC_STORAGE_EXPORT_READY)return true;
 /* A storage failure freezes its lease and all further storage callbacks.
  * Healthy controller custody remains available for failed CSW/sense/reset. */
 if(!faulted)error=reason;
 faulted=media_faulted=true;state=RISC_USB_MSC_FAULT_RETAINED;
 tud_msc_set_sense(0,SCSI_SENSE_MEDIUM_ERROR,sense_code,0);return false;
}
void tud_msc_inquiry_cb(uint8_t lun,uint8_t vendor[8],uint8_t product[16],uint8_t revision[4]) {
 (void)lun;memcpy(vendor,"RiscRTE ",8);memcpy(product,"SD Card         ",16);memcpy(revision,"0100",4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
 if(medium(lun))return true;
 tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0);return false;
}
void tud_msc_capacity_cb(uint8_t lun,uint32_t *count,uint16_t *size) {
 *count=medium(lun)?(uint32_t)blocks:0;*size=512;
}
bool tud_msc_is_writable_cb(uint8_t lun) { return medium(lun); }
int32_t tud_msc_read10_cb(uint8_t lun,uint32_t lba,uint32_t offset,void *buffer,uint32_t length) {
 if(!medium(lun) || !buffer || offset || length!=512 || (uint64_t)lba>=blocks)return -1;
 const uint64_t before=risc_msc_now();
 const int32_t result=volume->export_read(volume->sleep.terminal.power.volume.base.context,media_token,lba,1,buffer);
 record_io(lba,1,before,result);
 if(!media_result(result,"SD read failed during USB export",0x11))return -1;
 ++reads;return 512;
}
int32_t tud_msc_write10_cb(uint8_t lun,uint32_t lba,uint32_t offset,uint8_t *buffer,uint32_t length) {
 if(!medium(lun) || !buffer || offset || length!=512 || (uint64_t)lba>=blocks)return -1;
 const uint64_t before=risc_msc_now();
 const int32_t result=volume->export_write(volume->sleep.terminal.power.volume.base.context,media_token,lba,1,buffer);
 record_io(lba,1,before,result);
 if(!media_result(result,"SD write uncertain; exported card retained",0x0c))return -1;
 ++writes;return 512;
}
bool tud_msc_start_stop_cb(uint8_t lun,uint8_t power_condition,bool start,bool load_eject) {
 if(faulted || lun || power_condition || !media_token)return false;
 if(start)return !eject_requested;
 if(!load_eject)return true;
 if(prevent_removal){tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x53,2);return false;}
 const uint64_t before=risc_msc_now();
 const int32_t result=volume->export_sync(volume->sleep.terminal.power.volume.base.context,media_token);
 record_io(0,0,before,result);
 if(!media_result(result,"SD sync failed during host eject",0x0c))return false;
 eject_requested=true;return true;
}
void tud_msc_scsi_complete_cb(uint8_t lun,uint8_t const command[16]) {
 if(!lun && command[0]==0x1b && eject_requested)eject_complete=true;
}
int32_t tud_msc_scsi_cb(uint8_t lun,uint8_t const command[16],void *buffer,uint16_t length) {
 (void)buffer;(void)length;
 if(!medium(lun))return -1;
 switch(command[0]) {
  case 0x35: { /* SYNCHRONIZE CACHE(10), all writes already checked synchronously. */
   const uint64_t before=risc_msc_now();
   const int32_t result=volume->export_sync(volume->sleep.terminal.power.volume.base.context,media_token);
   record_io(0,0,before,result);
   return media_result(result,"SD cache sync failed",0x0c)?0:-1;
  }
  case 0x1e: /* PREVENT/ALLOW MEDIUM REMOVAL */
   if(command[4]&0xfe)break;
   prevent_removal=(command[4]&1)!=0;return 0;
 }
 tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x20,0);return -1;
}
void tud_mount_cb(void) { ever_configured=true;if(!faulted)state=RISC_USB_MSC_CONNECTED; }
void tud_umount_cb(void) { if(!faulted && !unplugged && !eject_requested)state=RISC_USB_MSC_WAITING; }
void tud_event_hook_cb(uint8_t port,uint32_t event,bool in_isr) {
 (void)port;(void)in_isr;
 if(event==DCD_EVENT_UNPLUGGED)unplugged=true;
 if(event==DCD_EVENT_BUS_RESET || event==DCD_EVENT_UNPLUGGED)command_active=false;
}
void tud_suspend_cb(bool remote_wakeup) { (void)remote_wakeup;if(!faulted && !eject_requested)state=RISC_USB_MSC_SUSPENDED; }
void tud_resume_cb(void) { if(!faulted && !eject_requested)state=ever_configured?RISC_USB_MSC_CONNECTED:RISC_USB_MSC_WAITING; }
/* Arduino-ESP32's existing default VID/PID pair is deliberately separate
 * from the boot console. A shipping product requires its assigned USB IDs. */
static const tusb_desc_device_t descriptor={
 .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
 .bDeviceClass=0,.bDeviceSubClass=0,.bDeviceProtocol=0,.bMaxPacketSize0=64,
 .idVendor=0x303a,.idProduct=0x0002,.bcdDevice=0x0100,.iManufacturer=1,.iProduct=2,.iSerialNumber=0,.bNumConfigurations=1
};
static const uint8_t configuration[]={
 TUD_CONFIG_DESCRIPTOR(1,1,0,TUD_CONFIG_DESC_LEN+TUD_MSC_DESC_LEN,0,100),
 TUD_MSC_DESCRIPTOR(0,0,0x01,0x81,64)
};
uint8_t const *tud_descriptor_device_cb(void) { return (const uint8_t *)&descriptor; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { return index==0?configuration:NULL; }
uint16_t const *tud_descriptor_string_cb(uint8_t index,uint16_t language) {
 (void)language;static uint16_t buffer[32];
 if(!index){buffer[0]=(TUSB_DESC_STRING<<8)|4;buffer[1]=0x0409;return buffer;}
 const char *s=index==1?"RiscRTE":index==2?"SD Card USB Storage":NULL;
 if(!s)return NULL;
 unsigned n=0;while(s[n] && n<31){buffer[1+n]=(uint8_t)s[n];++n;}
 buffer[0]=(uint16_t)((TUSB_DESC_STRING<<8)|(2*n+2));return buffer;
}
static bool start(const risc_provider_dependency_v1 *deps,size_t count) {
 if(started || poisoned || !deps || count!=3)return false;
 const risc_platform_clock_api_v1 *c=NULL;
 const risc_usb_phy_resource_api_v1 *p=NULL;
 const risc_storage_volume_api_v1_export_prepare *v=NULL;
 for(size_t i=0;i<count;++i) {
  if(!deps[i].capability_id || deps[i].api_version!=1 || !deps[i].api)return false;
  const char *id=deps[i].capability_id;
  if(!strcmp(id,"platform.clock") && !c)c=deps[i].api;
  else if(!strcmp(id,RISC_USB_PHY_RESOURCE_CAPABILITY) && !p)p=deps[i].api;
  else if(!strcmp(id,"storage.volume") && !v)v=risc_storage_volume_export_prepare(deps[i].api);
  else return false;
 }
 if(!c || c->api_version!=1 || c->struct_size<sizeof(*c) || !c->sleep_ms || !c->monotonic_ms ||
    !p || p->api_version!=1 || p->struct_size<sizeof(*p) || p->controller_kind!=RISC_USB_PHY_ESP32S3_OTG || p->reserved ||
    !p->is_owner || !p->claim || !p->release || !v || !p->is_owner(p->context))return false;
 clock_api=c;phy=p;preparation=v;volume=&v->base;started=true;return true;
}
static bool quiesce(void) {
 if(!started)return !session && !transport_live && !media_token && !phy_token;
 if(!enter())return false;
 if(session || transport_live || media_token || phy_token){(void)leave();return false;}
 if(!leave())return false;
 started=false;clock_api=NULL;phy=NULL;volume=NULL;preparation=NULL;return true;
}
static void stop(void) {}
static const risc_usb_device_msc_api_v1_diagnostics api={{{1,sizeof(api),NULL,begin,poll,end,last_error},RISC_USB_MSC_PREPARE_TAG,1,prepare_step},RISC_USB_MSC_DIAGNOSTICS_TAG,1,diagnostics};
static const risc_driver_v2 driver={RISC_PROVIDER_DRIVER_ABI_V2,sizeof(driver),"usb-device-msc-esp32s3",RISC_USB_DEVICE_MSC_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi==RISC_PROVIDER_DRIVER_ABI_V2?&driver:NULL; }
