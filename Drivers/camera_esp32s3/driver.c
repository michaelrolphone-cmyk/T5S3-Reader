#include "RiscCameraCaptureV1.h"
#include "RiscStreamProviderV1.h"
#include "T5StreamApi.h"
#include "hardware.h"
#include <string.h>
/* Each poll performs <=512 copied bytes and one bounded hardware transition.
 * Capture + consumer backpressure share an absolute <=15s deadline. */
static risc_stream_provider_v1 host;
static bool running, hardware_owned;
static uint64_t serial, job, began;
static uint32_t endpoint, timeout;
static risc_camera_status_v1 state;
static const uint8_t *frame;
static const char *error;
static void fail(int32_t result, const char *why) {
    state.state=RISC_CAMERA_FAILED; state.result=result; error=why;
    size_t n=strlen(why);if(n>=sizeof(state.detail))n=sizeof(state.detail)-1;
    memcpy(state.detail,why,n);state.detail[n]=0;
    if(endpoint) host.finish(host.context,endpoint,result);
}
static bool valid(uint64_t token) { return running && token && token==job; }
static int32_t capture(void *ctx,const risc_camera_request_v1 *r,uint64_t *out,uint32_t *stream) {
    (void)ctx; if(out)*out=0; if(stream)*stream=0;
    if(!running)return T5_STREAM_CLOSED;
    if(!r || !out || !stream || r->struct_size<sizeof(*r))return T5_STREAM_INVALID;
    if(job)return T5_STREAM_BUSY;
    if(r->format!=RISC_CAMERA_JPEG || r->width!=800 || r->height!=600 ||
       r->quality<4 || r->quality>40 || r->deadline_ms<500 || r->deadline_ms>15000)
        return T5_STREAM_UNSUPPORTED;
    if(serial==UINT64_MAX)return T5_STREAM_LIMIT;
    began=cam_hw_now(); if(began==UINT64_MAX)return T5_STREAM_IO;
    risc_stream_endpoint_v1 spec={sizeof(spec),T5_STREAM_BYTES,T5_STREAM_READ|T5_STREAM_WRITE,1024,NULL,0,0};
    int32_t rc=host.publish(host.context,&spec,&endpoint);
    if(rc!=T5_STREAM_OK){endpoint=0;return rc;}
    job=++serial; timeout=r->deadline_ms; frame=NULL;
    state=(risc_camera_status_v1){sizeof(state),RISC_CAMERA_CAPTURING,RISC_CAMERA_JPEG,800,600,0,0,T5_STREAM_AGAIN};
    if(!cam_hw_begin(r->quality))fail(T5_STREAM_IO,"capture start failed");
    *out=job; *stream=endpoint; return T5_STREAM_OK;
}
static int32_t read_frame(void *ctx,uint64_t token,void *bytes,uint32_t capacity,uint32_t *count) {
    (void)ctx;if(count)*count=0;
    if(!count || !bytes || !capacity || capacity>512)return T5_STREAM_INVALID;
    if(!valid(token))return T5_STREAM_CLOSED;
    return host.consume(host.context,endpoint,bytes,capacity,count);
}
static int32_t status(void *ctx,uint64_t token,risc_camera_status_v1 *out) {
    (void)ctx; if(!out || out->struct_size<offsetof(risc_camera_status_v1,detail))return T5_STREAM_INVALID;
    if(!valid(token))return T5_STREAM_CLOSED;
    size_t capacity=out->struct_size;if(capacity>sizeof(state))capacity=sizeof(state);
    memcpy(out,&state,capacity);return T5_STREAM_OK;
}
static int32_t cancel(void *ctx,uint64_t token) {
    (void)ctx;if(!valid(token))return T5_STREAM_CLOSED;
    if(!cam_hw_stop_capture()){fail(T5_STREAM_BUSY,"DMA stop uncertain");return T5_STREAM_BUSY;}
    if(state.state!=RISC_CAMERA_DONE)fail(T5_STREAM_CANCELLED,"capture cancelled");
    frame=NULL;return T5_STREAM_OK;
}
static int32_t release(void *ctx,uint64_t token) {
    if(!valid(token))return T5_STREAM_CLOSED;
    if(cancel(ctx,token)==T5_STREAM_BUSY)return T5_STREAM_BUSY;
    int32_t rc=host.close(host.context,endpoint);
    if(rc!=T5_STREAM_OK && rc!=T5_STREAM_CLOSED && rc!=T5_STREAM_DENIED)return rc;
    endpoint=0;job=0;frame=NULL;state=(risc_camera_status_v1){0};return T5_STREAM_OK;
}
static void poll(uint32_t budget_ms) {
    if(!budget_ms || !running || !job || state.state>=RISC_CAMERA_DONE)return;
    uint64_t now=cam_hw_now();
    if(now==UINT64_MAX || now<began || now-began>=timeout){
        const char *reason=state.state==RISC_CAMERA_CAPTURING?cam_hw_wait_reason():"stream output deadline";
        cam_hw_stop_capture();fail(T5_STREAM_TIMEOUT,reason);return;
    }
    if(state.state==RISC_CAMERA_CAPTURING){
        uint32_t length=0;int32_t rc=cam_hw_poll(&frame,&length);
        if(rc==T5_STREAM_OK){
            if(!frame || length<4 || length>96u*1024u)fail(T5_STREAM_IO,"invalid captured frame");
            else {state.length=length;state.state=RISC_CAMERA_DELIVERING;}
        }
        else if(rc!=T5_STREAM_AGAIN){cam_hw_stop_capture();fail(rc,"sensor capture failed");}
    }else if(state.state==RISC_CAMERA_DELIVERING){
        uint32_t count=state.length-state.transferred;if(count>512)count=512;
        uint32_t written=0;int32_t rc=host.produce(host.context,endpoint,frame+state.transferred,count,&written);
        if(written>count || (rc!=T5_STREAM_OK && written)){fail(T5_STREAM_IO,"invalid stream count");}
        else if(rc==T5_STREAM_OK){
            state.transferred+=written;
            if(state.transferred==state.length){
                rc=host.finish(host.context,endpoint,T5_STREAM_EOF);
                if(rc==T5_STREAM_OK){state.state=RISC_CAMERA_DONE;state.result=T5_STREAM_OK;}
                else fail(rc,"stream finish revoked");
            }
        }else if(rc!=T5_STREAM_AGAIN)fail(rc,"stream revoked or lost");
    }
    /* Returning after one bounded step lets the generic owner scheduler yield. */
}
static bool bind(const risc_stream_provider_v1 *h) {
    if(running || hardware_owned || !h || h->api_version!=1 || h->struct_size<sizeof(*h) ||
       !h->context || !h->publish || !h->produce || !h->consume || !h->finish || !h->close)return false;
    host=*h;return true;
}
static bool start(const risc_provider_dependency_v1 *d,size_t n) {
    if(running || hardware_owned || !host.context || n!=1 || !d || !d[0].capability_id ||
       strcmp(d[0].capability_id,RISC_CAMERA_ESP32S3_PROFILE) || d[0].api_version!=1 || !d[0].api)return false;
    const risc_camera_esp32s3_profile_v1 *p=d[0].api;
    if(p->api_version!=1 || p->struct_size<sizeof(*p))return false;
    /* Own cleanup even on partial startup; loader calls quiesce on failure. */
    hardware_owned=true;
    if(!cam_hw_start(p)){error="sensor/profile startup failed";return false;}
    running=true;error=NULL;return true;
}
static bool quiesce(void) {
    running=false;
    if(hardware_owned && !cam_hw_shutdown()){error="hardware shutdown uncertain";return false;}
    hardware_owned=false;frame=NULL;
    /* Host may already be revoked; it owns endpoint reclamation. */
    if(endpoint)host.close(host.context,endpoint);
    endpoint=0;job=0;return true;
}
static void stop(void) { if(quiesce())memset(&host,0,sizeof(host)); }
static bool last_error(char *out,size_t size) {
    if(!out || !size)return false;
    if(!error){*out=0;return false;}
    size_t n=strlen(error);if(n>=size)n=size-1;memcpy(out,error,n);out[n]=0;return true;
}
static const risc_camera_capture_api_v1 api={1,sizeof(api),NULL,capture,read_frame,status,cancel,release};
static const risc_driver_poll_v2 driver={{{2,sizeof(driver),"camera-esp32s3-ov3660",
    RISC_CAMERA_CAPTURE_CAPABILITY,1,&api,start,stop,quiesce},last_error,bind},poll};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi==2?&driver.streams.driver:NULL;
}
