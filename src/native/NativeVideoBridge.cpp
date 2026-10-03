#include "NativeVideoBridge.h"
#include <Board.h>
#include <T5VideoApi.h>
#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
#include <PlatformDisplayProvider.h>
extern "C" bool native_hardware_display_is_borrowed(void);
namespace {
const t5_video_api_v1 *video=nullptr;
bool start_format(t5_video_surface_v1 *surface,uint8_t format) {
    if(!native_hardware_display_is_borrowed())return false;
    if(!video) {
        const auto *provider=platformDisplayProvider();
        if(!provider)return false;
        video=provider->fast;
    }
    // The platform lease pins the provider even after partial failed startup.
    return video->start_format(surface,format);
}
bool start(t5_video_surface_v1*s){return start_format(s,T5_VIDEO_PIXEL_MONO_1BPP_MSB);}
uint8_t* buffer(size_t*n){if(n)*n=0;return video?video->backbuffer(n):nullptr;}
bool can_submit(){return video&&video->can_submit();}
bool submit(uint16_t y,uint16_t h){return video&&video->submit(y,h);}
bool pending(){return video&&video->pending();}
uint32_t counter(){return video?video->frame_counter():0;}
void stop(){(void)nativeVideoForceStop();}
bool stats(t5_video_scan_stats_v1*s){return video&&video->scan_stats(s);}
bool reinforce(uint16_t y,uint16_t h,uint8_t p){return video&&video->reinforce_black(y,h,p);}
const t5_video_api_v1 api={1,sizeof(api),start,buffer,can_submit,submit,pending,counter,stop,start_format,stats,reinforce,nativeVideoForceStop};
}
extern "C" const t5_video_api_v1 *t5_video_get_api(uint32_t version){return version==1?&api:nullptr;}
bool nativeVideoForceStop(){
    // Portable display.output clients can start the same engine without ever
    // entering this legacy facade. The platform lease pins that shared instance.
    if(!video && native_hardware_display_is_borrowed()) {
        const auto *provider=platformDisplayProvider();
        if(!provider)return false;
        video=provider->fast;
    }
    if(video&&!video->try_stop())return false;
    video=nullptr;return true;
}
#else
extern "C" const t5_video_api_v1 *t5_video_get_api(uint32_t){return nullptr;}
bool nativeVideoForceStop(){return true;}
#endif
