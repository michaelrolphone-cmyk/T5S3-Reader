#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
static const uint8_t config_single[]={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,0xff,0,0,0,7,5,0x81,2,64,0,0,7,5,0x02,2,64,0,0};
static uint16_t vendor=0x10c4;
static int claims,releases,controls,reads,writes,fail_request=-1;
static bool fail_release;
static uint8_t requests[24],speeds[4];
static uint16_t values[24];
static bool configuration(void *ctx,uint64_t dev,uint8_t *buf,size_t *size,uint16_t *vid,uint16_t *pid){(void)ctx;if(!dev||!buf||!size||*size<sizeof(config_single))return false;memcpy(buf,config_single,sizeof(config_single));*size=sizeof(config_single);*vid=vendor;*pid=0xea60;return true;}
static bool claim(void *ctx,uint64_t dev,uint8_t iface,uint8_t alt,uint64_t *out){(void)ctx;assert(dev==7&&iface==0&&alt==0&&out);*out=17;++claims;return true;}
static bool checked_release(void *ctx,uint64_t token){(void)ctx;assert(token==17);++releases;return !fail_release;}
static void release_claim(void *ctx,uint64_t token){(void)checked_release(ctx,token);}
static int32_t control(void *ctx,uint64_t dev,uint8_t type,uint8_t req,uint16_t value,uint16_t iface,uint8_t *data,uint16_t length,uint32_t timeout){(void)ctx;assert(dev==7&&type==0x41&&iface==0&&timeout==1000);assert(controls<24);requests[controls]=req;values[controls]=value;++controls;if(fail_request==req)return -1;if(req==0x1e){assert(data&&length==4);memcpy(speeds,data,4);}else assert(!data&&length==0);return length;}
static int32_t read_data(void *ctx,uint64_t token,uint8_t ep,uint8_t *dst,size_t cap,uint32_t timeout){(void)ctx;assert(token==17&&ep==0x81&&cap>=2&&timeout==30);++reads;dst[0]='O';dst[1]='K';return 2;}
static int32_t write_data(void *ctx,uint64_t token,uint8_t ep,const uint8_t *src,size_t length,uint32_t timeout){(void)ctx;assert(token==17&&ep==0x02&&length==2&&timeout==40&&src[0]=='H'&&src[1]=='I');++writes;return 2;}
int main(int argc,char **argv){
 assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW);assert(lib);risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(lib,"t5_driver_get");assert(get&&!get(1));const risc_driver_v2 *driver=get(RISC_PROVIDER_DRIVER_ABI_V2);assert(driver&&driver->struct_size==sizeof(*driver)&&strcmp(driver->driver_id,"usb-cp210x-v2")==0&&strcmp(driver->capability_id,"serial.port")==0&&driver->quiesce);const risc_usb_cdc_api_v1 *serial=(const risc_usb_cdc_api_v1 *)driver->capability;assert(serial&&serial->api_version==1&&serial->struct_size==sizeof(*serial));assert(!driver->start(NULL,0));
 risc_usb_host_api_v1 legacy={RISC_USB_HOST_API_V1,sizeof(legacy),NULL,configuration,claim,release_claim,control,read_data,write_data};risc_provider_dependency_v1 dep={"usb.host",1,&legacy};assert(!driver->start(&dep,1));
 risc_usb_host_discovery_v1 host={{RISC_USB_HOST_API_V1,sizeof(host),NULL,configuration,claim,release_claim,control,read_data,write_data},0,0,checked_release};dep.api=&host.host;assert(driver->start(&dep,1)&&!driver->start(&dep,1)&&driver->quiesce());
 vendor=0xffff;assert(!serial->open(7)&&claims==0);vendor=0x10c4;
 fail_request=0;assert(!serial->open(7)&&claims==1&&!driver->quiesce()&&releases==0);driver->stop();assert(!driver->quiesce());fail_request=-1;driver->stop();assert(releases==1);assert(driver->start(&dep,1));
 uint64_t first=serial->open(7);assert(first&&claims==2&&controls>=3&&!driver->quiesce());assert(!serial->configure(first,0,8,0,1)&&!serial->configure(first,115200,9,0,1));assert(serial->configure(first,115200,8,2,1));assert(speeds[0]==0&&speeds[1]==0xc2&&speeds[2]==1&&speeds[3]==0);assert(requests[controls-2]==0x1e&&requests[controls-1]==0x03&&values[controls-1]==0x0820);assert(serial->control_lines(first,true,false)&&requests[controls-1]==0x07&&values[controls-1]==0x0301);
 uint8_t payload[2]={0};assert(serial->read(first,payload,2,30)==2&&payload[0]=='O'&&payload[1]=='K');assert(serial->write(first,(const uint8_t *)"HI",2,40)==2&&reads==1&&writes==1);
 fail_request=0;assert(!serial->close(first)&&!driver->quiesce()&&releases==1);fail_request=-1;fail_release=true;assert(!serial->close(first)&&!driver->quiesce()&&releases==2);int commands=controls;assert(serial->read(first,payload,2,30)<0&&!serial->control_lines(first,true,true));fail_release=false;assert(serial->close(first)&&driver->quiesce()&&releases==3&&controls==commands);assert(!serial->close(first)&&serial->read(first,payload,2,30)<0);
 uint64_t next=serial->open(7);assert(next&&next!=first&&!serial->control_lines(first,true,true));assert(serial->close(next)&&driver->quiesce());driver->stop();assert(dlclose(lib)==0);puts("CP210x checked release, failed-open recovery, vendor I/O and stale handles: PASS");return 0;
}
