#include "ProviderEnvironment.hpp"
#include "Dispatcher.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

const risc_display_power_api_v1 *display_power=nullptr;
const risc_platform_clock_api_v1 *display_clock=nullptr;
extern bool display_quality_destroy();
namespace {
// All public calls serialize through this bounded gate. The scan workers touch
// their own engine state, never this dispatcher. No dynamic lock survives unload.
bool busy=false,active=false;
enum class Mode {none,quality,fast};
Mode mode=Mode::none;
class Gate {
    bool held_=false;
public:
    Gate() {
        if(xPortInIsrContext())return;
        const int64_t began=esp_timer_get_time();
        for(unsigned i=0;i<100;++i) {
            if(!__atomic_test_and_set(&busy,__ATOMIC_ACQUIRE)){held_=true;break;}
            if(esp_timer_get_time()-began>=100000)break;
            vTaskDelay(1);
        }
    }
    ~Gate(){if(held_)__atomic_clear(&busy,__ATOMIC_RELEASE);}
    explicit operator bool()const{return held_;}
};
const t5_display_quality_api_v1 *quality(){return display_quality_api();}
const t5_video_api_v1 *fast(){return display_fast_api(1);}
bool stop_mode() {
    bool okay=mode==Mode::none || (mode==Mode::quality?quality()->try_stop():fast()->try_stop());
    if(okay) {
        if(mode==Mode::fast)display_output_fast_stopped();
        mode=Mode::none;
    }
    return okay;
}
bool quality_start(bool clear) {
    Gate gate;if(!gate||!active||mode!=Mode::none)return false;
    mode=Mode::quality; // Pin even partially failed startup until checked stop.
    if(quality()->start(clear))return true;
    (void)stop_mode();return false;
}
bool quality_write(const uint8_t*p,size_t n,uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint8_t m) {
    Gate gate;return gate&&active&&mode==Mode::quality&&quality()->write_gray(p,n,x,y,w,h,m);
}
bool quality_wait(uint32_t ms){Gate gate;return gate&&active&&mode==Mode::quality&&quality()->wait(ms);}
bool quality_power(bool on){Gate gate;return gate&&active&&mode==Mode::quality&&quality()->set_power(on);}
bool quality_suppress(bool on){Gate gate;return gate&&active&&mode==Mode::quality&&quality()->suppress_output(on);}
bool quality_stop(){Gate gate;return gate&&(mode==Mode::none || (mode==Mode::quality&&stop_mode()));}
bool fast_start_format(t5_video_surface_v1*s,uint8_t f) {
    Gate gate;if(!gate||!active||mode==Mode::quality)return false;
    mode=Mode::fast;
    if(fast()->start_format(s,f))return true;
    (void)stop_mode();return false;
}
bool fast_start(t5_video_surface_v1*s){return fast_start_format(s,T5_VIDEO_PIXEL_MONO_1BPP_MSB);}
uint8_t *fast_backbuffer(size_t*n){Gate gate;if(n)*n=0;return gate&&active&&mode==Mode::fast?fast()->backbuffer(n):nullptr;}
bool fast_can(){Gate gate;return gate&&active&&mode==Mode::fast&&fast()->can_submit();}
bool fast_submit(uint16_t y,uint16_t h){Gate gate;return gate&&active&&mode==Mode::fast&&fast()->submit(y,h);}
bool fast_pending(){Gate gate;return !gate || (active&&mode==Mode::fast&&fast()->pending());}
uint32_t fast_counter(){Gate gate;return gate&&active&&mode==Mode::fast?fast()->frame_counter():0;}
bool fast_stop(){Gate gate;return gate&&(mode==Mode::none || (mode==Mode::fast&&stop_mode()));}
void fast_stop_legacy(){(void)fast_stop();}
bool fast_stats(t5_video_scan_stats_v1*s){Gate gate;return gate&&active&&mode==Mode::fast&&fast()->scan_stats(s);}
bool fast_reinforce(uint16_t y,uint16_t h,uint8_t p){Gate gate;return gate&&active&&mode==Mode::fast&&fast()->reinforce_black(y,h,p);}
}
extern "C" const t5_display_quality_api_v1 display_quality_dispatch={1,sizeof(t5_display_quality_api_v1),quality_start,quality_write,quality_wait,quality_power,quality_suppress,quality_stop};
extern "C" const t5_video_api_v1 display_fast_dispatch={1,sizeof(t5_video_api_v1),fast_start,fast_backbuffer,fast_can,fast_submit,fast_pending,fast_counter,fast_stop_legacy,fast_start_format,fast_stats,fast_reinforce,fast_stop};
extern "C" bool display_provider_start(const risc_provider_dependency_v1*deps,size_t count) {
    Gate gate;if(!gate||active||mode!=Mode::none||count!=2||!deps)return false;
    const risc_display_power_api_v1*p=nullptr;const risc_platform_clock_api_v1*c=nullptr;
    for(size_t i=0;i<count;++i) {
        if(deps[i].api_version!=1||!deps[i].capability_id||!deps[i].api)return false;
        if(!strcmp(deps[i].capability_id,"display.power")&&!p)p=static_cast<const risc_display_power_api_v1*>(deps[i].api);
        else if(!strcmp(deps[i].capability_id,"platform.clock")&&!c)c=static_cast<const risc_platform_clock_api_v1*>(deps[i].api);
        else return false;
    }
    if(!p||p->api_version!=1||p->struct_size<sizeof(*p)||!p->acquire||!p->release||
       !c||c->api_version!=1||c->struct_size<sizeof(*c)||!c->monotonic_ms||!c->sleep_ms)return false;
    display_power=p;display_clock=c;active=true;return true;
}
extern "C" bool display_provider_quiesce() {
    Gate gate;if(!gate)return false;
    active=false; // A failed cleanup retains the mode and dependency pointers.
    if(!stop_mode()||!display_quality_destroy())return false;
    display_power=nullptr;display_clock=nullptr;return true;
}
