#pragma once
#include <PlatformDisplayProvider.h>
#include <esp_heap_caps.h>
#include <lgfx/v1/misc/enum.hpp>
#include <stdint.h>

// Reader-side canvas and presentation intent only. No pins, LCD/DMA, panel
// history or waveform implementation belongs to this consumer.
class T5DisplayClient {
    const t5_display_quality_api_v1* api_=nullptr;
    bool failed_=false;
    uint8_t mode_=4;
    uint16_t x_=0,y_=0,w_=960,h_=540;
public:
    bool init(bool clear=true) {
        const auto *provider=platformDisplayProvider();
        if(!provider || !provider->quality)return false;
        api_=provider->quality;
        failed_=!api_->start(clear);return !failed_;
    }
    bool initPreservingPanel(){return init(false);}
    bool releaseHardware(){return !api_ || api_->try_stop();}
    void waitDisplay(){if(api_&&!api_->wait(3000))failed_=true;}
    bool setPowerChecked(bool on){return !failed_&&api_&&api_->set_power(on);}
    void powerSave(bool enabled){if(!setPowerChecked(!enabled))failed_=true;}
    void sleep(){} // Caller already requested checked external power shutdown.
    bool setPanelOutputSuppressed(bool value){return !failed_&&api_&&api_->suppress_output(value);}
    void setEpdMode(lgfx::epd_mode_t mode){mode_=static_cast<uint8_t>(mode);}
    void setClipRect(int x,int y,int w,int h){x_=x;y_=y;w_=w;h_=h;}
    void clearClipRect(){x_=y_=0;w_=960;h_=540;}
    bool present(const uint8_t*p) {
        if(failed_||!api_)return false;
        if(!api_->write_gray(p,960u*540u,x_,y_,w_,h_,mode_))failed_=true;
        return !failed_;
    }
    bool failed()const{return failed_;}
};
class T5DisplayCanvas {
    T5DisplayClient* client_;
    uint8_t* pixels_=nullptr;
public:
    explicit T5DisplayCanvas(T5DisplayClient*client):client_(client){}
    ~T5DisplayCanvas(){heap_caps_free(pixels_);}
    uint8_t* create(){
        pixels_=static_cast<uint8_t*>(heap_caps_malloc(960u*540u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        return pixels_;
    }
    uint8_t* getBuffer(){return pixels_;}
    void pushSprite(int,int){if(pixels_)client_->present(pixels_);}
    void pushSprite(T5DisplayClient*client,int,int){if(pixels_)client->present(pixels_);}
};
