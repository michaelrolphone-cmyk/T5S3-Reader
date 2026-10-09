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
static uint64_t phy_token,media_token,session,next_session;
static uint64_t blocks,reads,writes;
static uint32_t state=RISC_USB_MSC_IDLE;
static bool started,busy,poisoned,transport_live,ever_configured,eject_requested,eject_complete,unplugged;
static bool media_ready,prevent_removal,media_unknown,phy_unknown;
static const char *error;
static bool owner(void) { return phy && phy->is_owner(phy->context); }
static bool enter(void) {
 if(!owner() || busy || poisoned)return false;
 busy=true;return true;
}
static bool leave(void) {
 if(!owner()){poisoned=true;state=RISC_USB_MSC_FAULT_RETAINED;return false;}
 busy=false;return true;
}
static int32_t finish(int32_t result) { return leave()?result:RISC_USB_MSC_RETAINED; }
static void fault(const char *why) { error=why;state=RISC_USB_MSC_FAULT_RETAINED; }
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
static int32_t begin(void *context,uint64_t *out) {
 (void)context;if(out)*out=0;
 if(!out || !enter())return RISC_USB_MSC_REFUSED;
 if(!started || session || next_session==UINT64_MAX)return finish(RISC_USB_MSC_REFUSED);
 error=NULL;reads=writes=blocks=0;media_ready=false;
 uint32_t block_size=0;
 const int32_t result=volume->export_begin(volume->sleep.terminal.power.volume.base.context,&media_token,&blocks,&block_size);
 if(result!=RISC_STORAGE_EXPORT_READY) {
  if(media_token || result==RISC_STORAGE_EXPORT_RETAINED){media_unknown=!media_token;session=++next_session;*out=session;fault("SD ownership retained; USB export refused");return finish(RISC_USB_MSC_RETAINED);}
  error="Close local files and retry with a ready SD card";return finish(RISC_USB_MSC_REFUSED);
 }
 session=++next_session;*out=session;
 if(!media_token){media_unknown=true;fault("SD returned invalid ownership token");return finish(RISC_USB_MSC_RETAINED);}
 if(!blocks || blocks>UINT32_MAX || block_size!=512u) {
  error="SD geometry unsupported by USB READ CAPACITY(10)";
  if(!cleanup())return finish(RISC_USB_MSC_RETAINED);
  session=0;*out=0;return finish(RISC_USB_MSC_REFUSED);
 }
 const bool claimed=phy->claim(phy->context,&phy_token);
 if(claimed && !phy_token){phy_unknown=true;fault("USB PHY returned invalid ownership token");return finish(RISC_USB_MSC_RETAINED);}
 if(!claimed) {
  error="USB PHY is unavailable";
  if(phy_token){fault("USB PHY claim retained");return finish(RISC_USB_MSC_RETAINED);}
  if(!cleanup())return finish(RISC_USB_MSC_RETAINED);
  session=0;*out=0;return finish(RISC_USB_MSC_REFUSED);
 }
 ever_configured=eject_requested=eject_complete=unplugged=prevent_removal=false;
 /* Logical disconnect interval also covers a computer attached before boot. */
 clock_api->sleep_ms(clock_api->context,30);
 transport_live=true;
 if(!risc_msc_transport_start()) {
  error="USB controller start failed";
  if(!cleanup())return finish(RISC_USB_MSC_RETAINED);
  session=0;*out=0;return finish(RISC_USB_MSC_IO_ERROR);
 }
 state=RISC_USB_MSC_WAITING;return finish(RISC_USB_MSC_OK);
}
static int32_t poll(void *context,uint64_t token,risc_usb_device_msc_status_v1 *out) {
 (void)context;
 if(!out || out->struct_size<sizeof(*out) || !enter())return RISC_USB_MSC_REFUSED;
 if(!session || token!=session)return finish(RISC_USB_MSC_REFUSED);
 if(transport_live && state!=RISC_USB_MSC_FAULT_RETAINED) {
  if(!risc_msc_transport_poll())fault("USB controller fault; SD custody retained");
  if(state!=RISC_USB_MSC_FAULT_RETAINED && (eject_complete || unplugged)) {
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
static bool last_error(void *context,char *out,size_t capacity) {
 (void)context;if(!out || !capacity || !enter())return false;
 size_t n=0;if(error)while(error[n] && n+1<capacity){out[n]=error[n];++n;}out[n]=0;
 return leave() && n>0;
}
bool risc_msc_command_range(uint32_t lba,uint32_t count) {
 return media_token && (uint64_t)lba<blocks && (uint64_t)count<=blocks-lba;
}
static bool medium(uint8_t lun) { return lun==0 && media_token && state!=RISC_USB_MSC_FAULT_RETAINED && !eject_requested; }
static bool media_result(int32_t result,const char *reason) {
 if(result==RISC_STORAGE_EXPORT_READY)return true;
 fault(reason);tud_msc_set_sense(0,SCSI_SENSE_MEDIUM_ERROR,0x0c,0);return false;
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
 if(!media_result(volume->export_read(volume->sleep.terminal.power.volume.base.context,media_token,lba,1,buffer),"SD read failed during USB export"))return -1;
 ++reads;return 512;
}
int32_t tud_msc_write10_cb(uint8_t lun,uint32_t lba,uint32_t offset,uint8_t *buffer,uint32_t length) {
 if(!medium(lun) || !buffer || offset || length!=512 || (uint64_t)lba>=blocks)return -1;
 if(!media_result(volume->export_write(volume->sleep.terminal.power.volume.base.context,media_token,lba,1,buffer),"SD write uncertain; exported card retained"))return -1;
 ++writes;return 512;
}
bool tud_msc_start_stop_cb(uint8_t lun,uint8_t power_condition,bool start,bool load_eject) {
 if(lun || power_condition || !media_token)return false;
 if(start)return !eject_requested;
 if(!load_eject)return true;
 if(prevent_removal){tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x53,2);return false;}
 if(!media_result(volume->export_sync(volume->sleep.terminal.power.volume.base.context,media_token),"SD sync failed during host eject"))return false;
 eject_requested=true;return true;
}
void tud_msc_scsi_complete_cb(uint8_t lun,uint8_t const command[16]) {
 if(!lun && command[0]==0x1b && eject_requested)eject_complete=true;
}
int32_t tud_msc_scsi_cb(uint8_t lun,uint8_t const command[16],void *buffer,uint16_t length) {
 (void)buffer;(void)length;
 if(!medium(lun))return -1;
 switch(command[0]) {
  case 0x35: /* SYNCHRONIZE CACHE(10), all writes already checked synchronously. */
   return media_result(volume->export_sync(volume->sleep.terminal.power.volume.base.context,media_token),"SD cache sync failed")?0:-1;
  case 0x1e: /* PREVENT/ALLOW MEDIUM REMOVAL */
   if(command[4]&0xfe)break;
   prevent_removal=(command[4]&1)!=0;return 0;
 }
 tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x20,0);return -1;
}
void tud_mount_cb(void) { ever_configured=true;state=RISC_USB_MSC_CONNECTED; }
void tud_umount_cb(void) { if(!unplugged && !eject_requested)state=RISC_USB_MSC_WAITING; }
void tud_event_hook_cb(uint8_t port,uint32_t event,bool in_isr) {
 (void)port;(void)in_isr;
 if(event==DCD_EVENT_UNPLUGGED)unplugged=true;
}
void tud_suspend_cb(bool remote_wakeup) { (void)remote_wakeup;if(!eject_requested)state=RISC_USB_MSC_SUSPENDED; }
void tud_resume_cb(void) { if(!eject_requested)state=ever_configured?RISC_USB_MSC_CONNECTED:RISC_USB_MSC_WAITING; }
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
 const risc_storage_volume_api_v1_export *v=NULL;
 for(size_t i=0;i<count;++i) {
  if(!deps[i].capability_id || deps[i].api_version!=1 || !deps[i].api)return false;
  const char *id=deps[i].capability_id;
  if(!strcmp(id,"platform.clock") && !c)c=deps[i].api;
  else if(!strcmp(id,RISC_USB_PHY_RESOURCE_CAPABILITY) && !p)p=deps[i].api;
  else if(!strcmp(id,"storage.volume") && !v)v=risc_storage_volume_export(deps[i].api);
  else return false;
 }
 if(!c || c->api_version!=1 || c->struct_size<sizeof(*c) || !c->sleep_ms || !c->monotonic_ms ||
    !p || p->api_version!=1 || p->struct_size<sizeof(*p) || p->controller_kind!=RISC_USB_PHY_ESP32S3_OTG || p->reserved ||
    !p->is_owner || !p->claim || !p->release || !v || !p->is_owner(p->context))return false;
 clock_api=c;phy=p;volume=v;started=true;return true;
}
static bool quiesce(void) {
 if(!started)return !session && !transport_live && !media_token && !phy_token;
 if(!enter())return false;
 if(session || transport_live || media_token || phy_token){(void)leave();return false;}
 if(!leave())return false;
 started=false;clock_api=NULL;phy=NULL;volume=NULL;return true;
}
static void stop(void) {}
static const risc_usb_device_msc_api_v1 api={1,sizeof(api),NULL,begin,poll,end,last_error};
static const risc_driver_v2 driver={RISC_PROVIDER_DRIVER_ABI_V2,sizeof(driver),"usb-device-msc-esp32s3",RISC_USB_DEVICE_MSC_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi==RISC_PROVIDER_DRIVER_ABI_V2?&driver:NULL; }
