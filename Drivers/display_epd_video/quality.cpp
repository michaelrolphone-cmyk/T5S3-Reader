#include "ProviderEnvironment.hpp"
#include <lgfx/v1/platforms/esp32/Bus_EPD.h>
#include <lgfx/v1/platforms/esp32/Panel_EPD.hpp>
#include <lgfx/v1/misc/pixelcopy.hpp>
#include <freertos/semphr.h>
#include <new>
#include <limits.h>

namespace {
class PowerLock {
    SemaphoreHandle_t lock_;
public:
    explicit PowerLock(SemaphoreHandle_t lock):lock_(lock) {
        if(!lock_ || xSemaphoreTake(lock_,pdMS_TO_TICKS(100)+1)!=pdTRUE)lock_=nullptr;
    }
    ~PowerLock(){if(lock_)xSemaphoreGive(lock_);}
    explicit operator bool() const{return lock_!=nullptr;}
};
class QualityBus final : public lgfx::Bus_EPD {
    uint64_t grant_=0;
    bool suppressed_=false;
    SemaphoreHandle_t power_lock_=nullptr;
public:
    ~QualityBus(){if(power_lock_)vQueueDelete(power_lock_);}
    bool init() override {
        if(!display_power || !display_clock)return false;
        if(!power_lock_)power_lock_=xSemaphoreCreateMutex();
        return power_lock_ && lgfx::Bus_EPD::init();
    }
    bool powerControl(bool on) override {
        wait();
        if(failed())return false;
        PowerLock lock(power_lock_);
        if(!lock){_faulted=true;return false;}
        if(suppressed_){_pwr_on=on;return true;}
        if(on) {
            if(_pwr_on)return true;
            if(grant_){_faulted=true;return false;}
            gpio_set_level(static_cast<gpio_num_t>(config().pin_spv),1);
            if(!display_power->acquire(display_power->context,&grant_)) {_faulted=true;return false;}
            _pwr_on=true;return true;
        }
        if(grant_ && !display_power->release(display_power->context,grant_)) {_faulted=true;return false;}
        grant_=0;_pwr_on=false;gpio_set_level(static_cast<gpio_num_t>(config().pin_spv),0);return true;
    }
    bool suppress(bool value) {
        wait();if(failed())return false;
        PowerLock lock(power_lock_);if(!lock){_faulted=true;return false;}
        if(grant_ && !display_power->release(display_power->context,grant_)){_faulted=true;return false;}
        grant_=0;_pwr_on=false;suppressed_=value;return true;
    }
    void release() override {
        // Called only after the CPU worker join. Free no waveform buffers
        // until both controller teardown and this checked rail release finish.
        lgfx::Bus_EPD::release();
        if(!lgfx::Bus_EPD::released())return;
        if(grant_) {
            PowerLock lock(power_lock_);if(!lock)return;
            if(!display_power || !display_power->release(display_power->context,grant_))return;
            grant_=0;
        }
        _pwr_on=false;
    }
    bool released() const override {return !grant_ && lgfx::Bus_EPD::released();}
};
struct Quality {
    // Panel destruction precedes its bus. Failed cleanup retains this object.
    QualityBus bus;
    lgfx::Panel_EPD panel;
    Quality() {
        auto cfg=bus.config();cfg.bus_speed=16000000;cfg.bus_width=8;
        const int8_t data[8]={5,6,7,15,16,17,18,8};
        for(unsigned i=0;i<8;++i)cfg.pin_data[i]=data[i];
        cfg.pin_pwr=-1;cfg.pin_oe=-1;cfg.pin_sph=41;cfg.pin_spv=45;
        cfg.pin_le=42;cfg.pin_cl=4;cfg.pin_ckv=48;bus.config(cfg);
        panel.setBus(&bus);
        auto detail=panel.config_detail();detail.line_padding=8;panel.config_detail(detail);
        auto pc=panel.config();pc.memory_width=pc.panel_width=960;
        pc.memory_height=pc.panel_height=540;pc.offset_rotation=0;
        pc.offset_x=pc.offset_y=0;pc.bus_shared=false;panel.config(pc);
    }
};
Quality *state=nullptr;
SemaphoreHandle_t control=nullptr;
// Provider mode ownership is reserved by the top-level dispatcher before start.
bool wait_locked(uint32_t timeout) {
    if(!state || timeout>3000)return false;
    const int64_t began=esp_timer_get_time();unsigned polls=0;
    while(!state->panel.completed()) {
        if(state->panel.failed() || !timeout || ++polls>timeout || esp_timer_get_time()-began>=int64_t(timeout)*1000)
            return false;
        vTaskDelay(1);
    }
    return true;
}
bool stop_locked() {
    if(!state)return true;
    if(!state->panel.shutdown())return false;
    state->~Quality();heap_caps_free(state);state=nullptr;return true;
}
bool start(bool clear) {
    if(xPortInIsrContext() || !display_power || !display_clock)return false;
    if(!control)control=xSemaphoreCreateMutex();
    PowerLock lock(control);if(!lock || state)return false;
    void *memory=heap_caps_malloc(sizeof(Quality),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!memory)return false;
    state=new(memory)Quality();
    if(!state->panel.init(true)){(void)stop_locked();return false;}
    state->panel.setRotation(0);state->panel.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    if(clear) {
        state->panel.setEpdMode(lgfx::epd_mode::epd_quality);
        state->panel.writeFillRectPreclipped(0,0,960,540,0xff);
        state->panel.display(0,0,960,540);
        if(!wait_locked(3000)){(void)stop_locked();return false;}
    }
    return true;
}
bool write_gray(const uint8_t *canvas,size_t bytes,uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint8_t mode) {
    if(xPortInIsrContext() || !canvas || bytes!=960u*540u || !w || !h || x>=960 || y>=540 ||
       w>960-x || h>540-y || mode<1 || mode>4)return false;
    PowerLock lock(control);if(!lock || !state || state->panel.failed())return false;
    lgfx::pixelcopy_t copy(canvas,lgfx::color_depth_t::grayscale_8bit,lgfx::color_depth_t::grayscale_8bit);
    copy.src_x=x;copy.src_y=y;copy.src_bitwidth=960;
    state->panel.setEpdMode(static_cast<lgfx::epd_mode_t>(mode));
    state->panel.writeImage(x,y,w,h,&copy,false);
    state->panel.display(x,y,w,h);
    return !state->panel.failed();
}
bool wait(uint32_t timeout) {if(xPortInIsrContext())return false;PowerLock lock(control);return lock && wait_locked(timeout);}
bool power(bool on) {if(xPortInIsrContext())return false;PowerLock lock(control);return lock && wait_locked(3000) && state->bus.powerControl(on);}
bool suppress(bool value) {if(xPortInIsrContext())return false;PowerLock lock(control);return lock && wait_locked(3000) && state->bus.suppress(value);}
bool stop() {if(xPortInIsrContext())return false;if(!control)return !state;PowerLock lock(control);return lock && stop_locked();}
const t5_display_quality_api_v1 api={1,sizeof(api),start,write_gray,wait,power,suppress,stop};
}
extern "C" const t5_display_quality_api_v1 *display_quality_api(void) {return &api;}
// Dispatcher has revoked/serialized every caller before this unload cleanup.
bool display_quality_destroy() {
    if(!stop())return false;
    if(control){vQueueDelete(control);control=nullptr;}
    return true;
}
