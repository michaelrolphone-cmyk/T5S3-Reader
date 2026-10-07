#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <T5AppApi.h>
#include <T5ProviderCapabilityApi.h>
#include <T5StreamApi.h>
#include <RiscCameraCaptureV1.h>
#define app_main camera_utility_entry
#include "../../Apps/camera_utility.c"
#undef app_main
static unsigned polls, reads, captures, releases, cancels, opens, finishes, closes;
static uint8_t written[16];
static uint32_t written_size;
static int fail_read;
static unsigned occupied_slots;
static char saved_path[48];
static bool mock_poll(t5_app_input_t* input,uint32_t wait){(void)wait;*input=(t5_app_input_t){0};polls++;return polls<100;}
static uint32_t mock_millis(void){return polls*20;}
static bool mock_acquire(const char* name,uint32_t version,t5_provider_capability_lease_t* token,const void** iface){
 assert(strcmp(name,"camera.capture")==0 && version==1);*token=1;*iface=camera;return true;
}
static bool mock_release_lease(t5_provider_capability_lease_t token){assert(token==1);return true;}
static int32_t mock_capture(void* ctx,const risc_camera_request_v1* req,uint64_t* out,uint32_t* endpoint){
 (void)ctx;assert(req->format==RISC_CAMERA_JPEG);captures++;*out=1;*endpoint=1;return T5_STREAM_OK;
}
static int32_t mock_status(void* ctx,uint64_t token,risc_camera_status_v1* status){
 (void)ctx;assert(token==1);status->state=RISC_CAMERA_DONE;status->length=4;return T5_STREAM_OK;
}
static int32_t mock_read(void* ctx,uint64_t token,void* data,uint32_t cap,uint32_t* count){
 (void)ctx;assert(token==1 && cap>=4);reads++;*count=0;
 if(fail_read)return T5_STREAM_IO;
 if(reads==1){const uint8_t jpeg[]={0xff,0xd8,0xff,0xd9};memcpy(data,jpeg,4);*count=4;return T5_STREAM_OK;}
 return T5_STREAM_EOF;
}
static int32_t mock_cancel(void* ctx,uint64_t token){(void)ctx;assert(token==1);cancels++;return T5_STREAM_OK;}
static int32_t mock_release_job(void* ctx,uint64_t token){(void)ctx;assert(token==1);releases++;return T5_STREAM_OK;}
static int32_t mock_open(const char* path,uint32_t mode,t5_stream_t* out){
 assert(mode==T5_STREAM_FILE_CREATE_NEW);opens++;strcpy(saved_path,path);
 if(opens<=occupied_slots)return T5_STREAM_IO;
 *out=1;return T5_STREAM_OK;
}
static int32_t mock_write(t5_stream_t stream,const void* bytes,uint32_t size,uint32_t* count){
 assert(stream==1 && written_size+size<=sizeof(written));memcpy(written+written_size,bytes,size);
 written_size+=size;*count=size;return T5_STREAM_OK;
}
static int32_t mock_finish(t5_stream_t stream){assert(stream==1);finishes++;return T5_STREAM_OK;}
static int32_t mock_close(t5_stream_t stream){assert(stream==1);closes++;return T5_STREAM_OK;}
const t5_app_api_v1* t5_app_get_api(uint32_t version){
 static t5_app_api_v1 api;assert(version==1);api.abi_version=1;api.struct_size=sizeof(api);
 api.poll=mock_poll;api.millis=mock_millis;return &api;
}
const t5_stream_api_v1* t5_stream_get_api(uint32_t version){
 static t5_stream_api_v1 api;assert(version==1);api.api_version=1;api.struct_size=sizeof(api);
 api.open_file=mock_open;api.write=mock_write;api.finish=mock_finish;api.close=mock_close;return &api;
}
const t5_provider_capability_api_v1* t5_provider_capability_get_api(uint32_t version){
 static t5_provider_capability_api_v1 api;assert(version==1);api.api_version=1;api.struct_size=sizeof(api);
 api.acquire=mock_acquire;api.release=mock_release_lease;return &api;
}
static const risc_camera_capture_api_v1 mock_camera={
 .api_version=1,.struct_size=sizeof(risc_camera_capture_api_v1),.capture=mock_capture,
 .read=mock_read,.status=mock_status,.cancel=mock_cancel,.release=mock_release_job};
int main(void){
 camera=&mock_camera;camera_utility_entry();
 assert(captures==1 && reads==2 && releases==1 && cancels==0);
 assert(opens==1 && finishes==1 && closes==1 && written_size==4);
 assert(memcmp(written,"\xff\xd8\xff\xd9",4)==0);
 assert(strcmp(saved_path,"/sd/camera-utility-0001.jpg")==0);
 polls=reads=captures=releases=cancels=opens=finishes=closes=written_size=0;occupied_slots=16;
 camera_utility_entry();
 assert(captures==1 && reads==2 && releases==1 && cancels==0);
 assert(opens==17 && finishes==1 && closes==1 && written_size==4);
 assert(strcmp(saved_path,"/sd/camera-utility-0017.jpg")==0);
 occupied_slots=0;
 polls=reads=captures=releases=cancels=opens=finishes=closes=written_size=0;fail_read=1;
 camera_utility_entry();
 assert(captures==1 && reads==1 && cancels==1 && releases==1 && opens==0);
}
