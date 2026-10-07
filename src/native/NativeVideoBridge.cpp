#include "NativeVideoBridge.h"
#include <Board.h>
#include <T5VideoApi.h>
#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
#include "NativeStreamBridge.h"
#include "NativeTouchInput.h"
#include <PlatformDisplayProvider.h>
extern "C" bool native_hardware_display_is_borrowed(void);
namespace {
const t5_video_api_v1 *video=nullptr;
// One outstanding observation, not another frame/input queue. The platform
// lease retains the provider; no app pointer or callback survives its owner.
bool awaitingTouchPresentation = false;
uint32_t presentationOwner = 0, presentationEpoch = 0, presentationCounter = 0;
bool start_format(t5_video_surface_v1 *surface,uint8_t format) {
    if(!native_hardware_display_is_borrowed())return false;
    if(!video) {
        const auto *provider=platformDisplayProvider();
        if(!provider)return false;
        video=provider->fast;
    }
    // Restart (including a failed attempt) cancels evidence from the previous
    // scan lifetime. A new accepted frame must establish readiness again.
    awaitingTouchPresentation = false;
    // The platform lease pins the provider even after partial failed startup.
    return video->start_format(surface,format);
}
bool start(t5_video_surface_v1*s){return start_format(s,T5_VIDEO_PIXEL_MONO_1BPP_MSB);}
uint8_t* buffer(size_t*n){if(n)*n=0;return video?video->backbuffer(n):nullptr;}
bool can_submit(){return video&&video->can_submit();}
bool submit(uint16_t y,uint16_t h) {
    if (!video) return false;
    const uint32_t owner = nativeProviderStreamConsumer();
    uint32_t epoch = 0;
    const bool observe = owner && native_hardware_display_is_borrowed() &&
        nativeTouchAwaitingSurfacePresentation(epoch) && video->pending &&
        video->can_submit && video->frame_counter;
    const uint32_t before = observe ? video->frame_counter() : 0;
    if (!video->submit(y,h)) return false;
    if (observe && (!awaitingTouchPresentation || presentationOwner != owner || presentationEpoch != epoch)) {
        presentationOwner = owner;
        presentationEpoch = epoch;
        presentationCounter = before;
        awaitingTouchPresentation = true;
    }
    return true;
}
bool pending(){return video&&video->pending();}
uint32_t counter(){return video?video->frame_counter():0;}
void stop(){(void)nativeVideoForceStop();}
bool stats(t5_video_scan_stats_v1*s){return video&&video->scan_stats(s);}
bool reinforce(uint16_t y,uint16_t h,uint8_t p){return video&&video->reinforce_black(y,h,p);}
const t5_video_api_v1 api={1,sizeof(api),start,buffer,can_submit,submit,pending,counter,stop,start_format,stats,reinforce,nativeVideoForceStop};
}
extern "C" const t5_video_api_v1 *t5_video_get_api(uint32_t version){return version==1?&api:nullptr;}
void nativeVideoServiceTouchPresentation() {
    if (!awaitingTouchPresentation) return;
    uint32_t epoch = 0;
    if (!video || !native_hardware_display_is_borrowed() ||
        nativeProviderStreamConsumer() != presentationOwner ||
        !nativeTouchAwaitingSurfacePresentation(epoch) || epoch != presentationEpoch) {
        awaitingTouchPresentation = false;
        return;
    }
    // frame_counter advances at scan START, not DMA completion. pending stays
    // true through queued flips and remaining pixel drive; can_submit also
    // proves the engine is still running (failure/stop alone can clear pending).
    // Each provider call is bounded; never wait/spin in the UI input path.
    if (video->pending() || !video->can_submit() || video->frame_counter() == presentationCounter) return;
    if (native_hardware_display_is_borrowed() && nativeProviderStreamConsumer() == presentationOwner) {
        const uint32_t completedEpoch = presentationEpoch;
        awaitingTouchPresentation = false;
        nativeTouchSurfacePresented(completedEpoch, true);
    }
}

bool nativeVideoForceStop(){
    // Even failed teardown revokes this observation. A later old scan must not
    // enable a retained, cancelled or next application's coordinate surface.
    awaitingTouchPresentation = false;
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
void nativeVideoServiceTouchPresentation(){}
bool nativeVideoForceStop(){return true;}
#endif
