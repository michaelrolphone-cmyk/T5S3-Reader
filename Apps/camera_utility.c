#include <T5AppApi.h>
#include <T5ProviderCapabilityApi.h>
#include <T5StreamApi.h>
#include <RiscCameraCaptureV1.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FRAME_CAPACITY (96u*1024u)
#define CAPTURE_DEADLINE_MS 60000u
static uint8_t frame[FRAME_CAPACITY];
static const t5_app_api_v1* app;
static const t5_stream_api_v1* streams;
static const t5_provider_capability_api_v1* providers;
static const risc_camera_capture_api_v1* camera;
static t5_provider_capability_lease_t lease;
static uint64_t job;

static bool tick(uint32_t started) {
    t5_app_input_t input={0};
    if(!app->poll(&input,20) || input.exit_requested)return false;
    return (uint32_t)(app->millis()-started)<CAPTURE_DEADLINE_MS;
}

static bool save(uint32_t length) {
    // CREATE_NEW never replaces a prior image. Only a complete JPEG reaches
    // output; partial I/O failures remain visibly diagnosable by their log.
    for(unsigned index=1;index<=9999;index++){
        char path[48];
        snprintf(path,sizeof(path),"/sd/camera-utility-%04u.jpg",index);
        t5_stream_t output=0;
        if(streams->open_file(path,T5_STREAM_FILE_CREATE_NEW,&output)!=T5_STREAM_OK)continue;
        uint32_t at=0;
        const uint32_t started=app->millis();
        bool good=true;
        while(at<length && good){
            uint32_t n=0;
            const uint32_t chunk=length-at>T5_STREAM_CHUNK?T5_STREAM_CHUNK:length-at;
            const int32_t rc=streams->write(output,frame+at,chunk,&n);
            if(rc==T5_STREAM_AGAIN){good=tick(started);continue;}
            if(rc!=T5_STREAM_OK || !n || n>chunk){good=false;break;}
            at+=n;
            good=tick(started);
        }
        if(good && at==length)good=streams->finish(output)==T5_STREAM_OK;
        if(streams->close(output)!=T5_STREAM_OK)good=false;
        if(good){printf("CAMERA_APP saved=%s bytes=%u\n",path,(unsigned)length);return true;}
        printf("CAMERA_APP output-failed=%s bytes=%u\n",path,(unsigned)at);
        return false;
    }
    printf("CAMERA_APP output-unavailable\n");return false;
}

void app_main(void) {
    app=t5_app_get_api(T5_APP_ABI_VERSION);
    streams=t5_stream_get_api(T5_STREAM_API_VERSION);
    providers=t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if(!app || !app->poll || !app->millis || !streams || !streams->open_file ||
       !streams->write || !streams->finish || !streams->close ||
       !providers || !providers->acquire || !providers->release){
        printf("CAMERA_APP unavailable-api\n");return;
    }
    const void* borrowed=0;
    if(!providers->acquire(RISC_CAMERA_CAPTURE_CAPABILITY,RISC_CAMERA_CAPTURE_API_V1,
                           &lease,&borrowed)){
        printf("CAMERA_APP capability-unavailable\n");return;
    }
    camera=(const risc_camera_capture_api_v1*)borrowed;
    if(camera->api_version!=RISC_CAMERA_CAPTURE_API_V1 ||
       camera->struct_size<sizeof(*camera) || !camera->capture || !camera->read ||
       !camera->status || !camera->cancel || !camera->release)goto cleanup;
    {
        const risc_camera_request_v1 request={sizeof(request),RISC_CAMERA_JPEG,800,600,12,15000};
        uint32_t endpoint=0;
        if(camera->capture(camera->context,&request,&job,&endpoint)!=T5_STREAM_OK)goto cleanup;
    }
    {
        uint32_t total=0;
        const uint32_t started=app->millis();
        bool complete=false;
        while(tick(started)){
            risc_camera_status_v1 status={.struct_size=sizeof(status)};
            if(camera->status(camera->context,job,&status)!=T5_STREAM_OK ||
               status.state==RISC_CAMERA_FAILED)break;
            uint32_t n=0;
            if(total==FRAME_CAPACITY)break;
            uint32_t capacity=FRAME_CAPACITY-total;
            if(capacity>T5_STREAM_CHUNK)capacity=T5_STREAM_CHUNK;
            const int32_t rc=camera->read(camera->context,job,frame+total,capacity,&n);
            if(rc==T5_STREAM_AGAIN)continue;
            if(rc==T5_STREAM_EOF){
                complete=status.state==RISC_CAMERA_DONE && total==status.length;
                break;
            }
            if(rc!=T5_STREAM_OK || !n || n>capacity)break;
            total+=n;
        }
        if(complete && total>=4 && frame[0]==0xff && frame[1]==0xd8 &&
           frame[total-2]==0xff && frame[total-1]==0xd9){
            // End the capture job before writing. The file stream and camera
            // lease have independent owner-bound cleanup paths.
            if(camera->release(camera->context,job)==T5_STREAM_OK){job=0;(void)save(total);}
        } else printf("CAMERA_APP capture-incomplete bytes=%u\n",(unsigned)total);
    }
cleanup:
    if(job){
        (void)camera->cancel(camera->context,job);
        if(camera->release(camera->context,job)!=T5_STREAM_OK)
            printf("CAMERA_APP capture-release-pending\n");
        job=0;
    }
    if(lease && !providers->release(lease))printf("CAMERA_APP provider-release-pending\n");
    lease=0;
}
