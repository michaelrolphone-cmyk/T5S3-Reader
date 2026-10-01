#include "RiscCameraCaptureV1.h"
#include "RiscCameraEsp32s3ProfileV1.h"
#include "RiscStreamProviderV1.h"
#include "T5StreamApi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool backend_start=true,backend_begin=true,can_stop=true,revoked,blocked;
static uint64_t now;
static int32_t backend_result=T5_STREAM_OK,finish_result;
static unsigned shutdowns,stops,yields,produces,closed,offset;
static uint8_t pixels[1300];
bool cam_hw_start(const risc_camera_esp32s3_profile_v1 *p){assert(p->sensor_pid==0x3660);return backend_start;}
bool cam_hw_begin(unsigned q){assert(q==12);return backend_begin;}
int32_t cam_hw_poll(const uint8_t **p,uint32_t *n){*p=pixels;*n=sizeof(pixels);return backend_result;}
bool cam_hw_stop_capture(void){stops++;return can_stop;}
bool cam_hw_shutdown(void){shutdowns++;return can_stop;}
uint64_t cam_hw_now(void){return now;}
void cam_hw_yield(void){yields++;now++;}
static int32_t publish(uint64_t c,const risc_stream_endpoint_v1 *s,uint32_t *out){
 assert(c==17 && s->kind==1 && s->byte_capacity==1024);if(revoked)return -2;*out=41;return 0;
}
static int32_t produce(uint64_t c,uint32_t h,const void *bytes,uint32_t n,uint32_t *out){
 assert(c==17 && h==41 && n<=512);*out=0;produces++;
 if(revoked)return T5_STREAM_DENIED;
 if(blocked)return T5_STREAM_AGAIN;
 assert(!memcmp(bytes,pixels+offset,n));offset+=n;*out=n;return 0;
}
static int32_t consume(uint64_t c,uint32_t h,void *bytes,uint32_t n,uint32_t *count){
 assert(c==17 && h==41 && n<=512);*count=0;if(revoked)return -2;
 unsigned take=offset<n?offset:n;memcpy(bytes,pixels,take);*count=take;return take?0:1;
}
static int32_t finish(uint64_t c,uint32_t h,int32_t r){assert(c==17 && h==41);(void)r;return revoked?-2:finish_result;}
static int32_t close_stream(uint64_t c,uint32_t h){assert(c==17 && h==41);closed++;return revoked?-2:0;}
static risc_stream_provider_v1 host={1,sizeof(host),17,publish,produce,consume,NULL,NULL,finish,close_stream};
static risc_camera_esp32s3_profile_v1 profile={.api_version=1,.struct_size=sizeof(profile),.sensor_pid=0x3660};
static risc_provider_dependency_v1 dep={RISC_CAMERA_ESP32S3_PROFILE,1,&profile};
static risc_camera_request_v1 request={sizeof(request),1,800,600,12,500};
static const risc_driver_poll_v2 *driver;
static const risc_camera_capture_api_v1 *api;
static void reset(void){
 driver->streams.driver.stop();now=10;revoked=blocked=false;can_stop=backend_start=backend_begin=true;
 backend_result=finish_result=0;offset=produces=0;
 assert(driver->streams.bind_streams(&host));assert(driver->streams.driver.start(&dep,1));
}
static uint64_t begin(void){uint64_t j=0;uint32_t h=0;assert(api->capture(NULL,&request,&j,&h)==0 && j && h==41);return j;}
static risc_camera_status_v1 status(uint64_t j){risc_camera_status_v1 s={.struct_size=sizeof(s)};assert(api->status(NULL,j,&s)==0);return s;}
int main(void){
 driver=(const risc_driver_poll_v2*)t5_driver_get(2);assert(driver && !t5_driver_get(1));
 assert(driver->streams.driver.struct_size==sizeof(*driver));api=driver->streams.driver.capability;
 for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=(uint8_t)i;
 assert(!driver->streams.driver.start(&dep,1));
 risc_stream_provider_v1 bad=host;bad.produce=NULL;assert(!driver->streams.bind_streams(&bad));
 reset();uint64_t j=0;uint32_t h=0;
 request.width=640;assert(api->capture(NULL,&request,&j,&h)==T5_STREAM_UNSUPPORTED && !j && !h);request.width=800;
 j=begin();uint64_t other;assert(api->capture(NULL,&request,&other,&h)==T5_STREAM_BUSY);
 driver->poll(0);assert(status(j).state==RISC_CAMERA_CAPTURING);
 driver->poll(1);assert(status(j).state==RISC_CAMERA_DELIVERING);
 blocked=true;driver->poll(1);assert(!status(j).transferred);blocked=false;
 for(unsigned i=0;i<3;i++)driver->poll(1);
 assert(status(j).state==RISC_CAMERA_DONE && status(j).transferred==sizeof(pixels));
 uint8_t copy[512];uint32_t got=99;
 assert(api->read(NULL,j,copy,513,&got)==T5_STREAM_INVALID && got==0);
 assert(api->read(NULL,j,copy,512,&got)==0 && got==512 && !memcmp(copy,pixels,512));
 assert(api->release(NULL,j)==0);risc_camera_status_v1 s={.struct_size=sizeof(s)};
 assert(api->status(NULL,j,&s)==T5_STREAM_CLOSED);uint64_t next=begin();assert(next!=j);assert(api->cancel(NULL,j)==T5_STREAM_CLOSED);
 assert(api->cancel(NULL,next)==0 && status(next).result==T5_STREAM_CANCELLED);assert(api->release(NULL,next)==0);
 reset();j=begin();backend_result=T5_STREAM_AGAIN;driver->poll(1);now=510;driver->poll(1);
 assert(status(j).state==RISC_CAMERA_FAILED && status(j).result==T5_STREAM_TIMEOUT);assert(api->release(NULL,j)==0);
 reset();j=begin();driver->poll(1);blocked=true;now=510;driver->poll(1);assert(status(j).result==T5_STREAM_TIMEOUT);
 reset();j=begin();driver->poll(1);revoked=true;driver->poll(1);assert(status(j).result==T5_STREAM_DENIED);assert(api->release(NULL,j)==0);
 reset();j=begin();can_stop=false;assert(api->cancel(NULL,j)==T5_STREAM_BUSY);assert(api->release(NULL,j)==T5_STREAM_BUSY);
 assert(!driver->streams.driver.quiesce());assert(!driver->streams.bind_streams(&host));
 can_stop=true;assert(driver->streams.driver.quiesce());driver->streams.driver.stop();
 reset();backend_begin=false;j=begin();assert(status(j).result==T5_STREAM_IO);assert(api->release(NULL,j)==0);
 reset();j=begin();backend_result=T5_STREAM_LIMIT;driver->poll(1);assert(status(j).result==T5_STREAM_LIMIT);
 reset();j=begin();driver->poll(1);finish_result=T5_STREAM_DENIED;for(unsigned i=0;i<3;i++)driver->poll(1);
 assert(status(j).result==T5_STREAM_DENIED);
 reset();j=begin();now=UINT64_MAX;driver->poll(1);assert(status(j).result==T5_STREAM_TIMEOUT);
 driver->streams.driver.stop();backend_start=false;assert(driver->streams.bind_streams(&host));
 assert(!driver->streams.driver.start(&dep,1));can_stop=false;assert(!driver->streams.driver.quiesce());
 can_stop=true;assert(driver->streams.driver.quiesce());driver->streams.driver.stop();
 assert(shutdowns && stops && !yields && closed);puts("camera provider: lifecycle, exact bytes, bounds, backpressure, timeout, revoke, stale jobs and quarantine passed");
}
