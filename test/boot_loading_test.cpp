// Compile the real StartupScreen implementation against bounded host doubles.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>
#include <cstdio>
#include <T5HardwareTakeover.h>
#include <T5VideoApi.h>
using Clock = std::chrono::steady_clock;
static const auto epoch = Clock::now();
uint32_t millis() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-epoch).count(); }
void delay(unsigned n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }
namespace Color { enum { Black }; }
namespace EpdFontFamily { enum { BOLD }; }
[[maybe_unused]] static struct { void suppressInitialFullRefresh(){} } display;
enum class DisplayPresentMode { Quality, LowLatency, Clean };
constexpr int UI_12_FONT_ID=0, SMALL_FONT_ID=1;
struct GfxRenderer {
 enum { BW }; int presents=0;
 int getRenderMode(){return BW;} void setRenderMode(int){}
 void clearScreen(){} int getScreenWidth(){return 540;} int getScreenHeight(){return 960;}
 void fillRoundedRect(int,int,int,int,int,int){}
 void drawCenteredText(int,int,const char*,bool=false,int=0){}
 void displayBuffer(DisplayPresentMode){++presents;}
 void requestNextRefresh(DisplayPresentMode){}
};
using esp_err_t=int;
constexpr int ESP_OK=0;
const char* esp_err_to_name(int){return "mock failure";}
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
using TaskHandle_t=void*;
using BaseType_t=int;
constexpr int pdPASS=1;
static std::thread worker;
static bool deferWorker=false,failCreate=false,failEnd=false;
static std::atomic<bool> canSubmit{true}, stopped{false};
static unsigned stops=0,ends=0;
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void*),const char*,int,void* arg,int,TaskHandle_t* out,int){
 if(failCreate)return 0;
 *out=reinterpret_cast<void*>(1);
 if(!deferWorker)worker=std::thread(fn,arg);
 return pdPASS;
}
void vTaskDelete(TaskHandle_t t){assert(t==nullptr);}
static uint8_t pixels[960*540/8];
static uint32_t lastSubmit=0;
static bool startVideo(t5_video_surface_v1* s){
 stopped=false;lastSubmit=0;
 *s={960,540,120,T5_VIDEO_PIXEL_MONO_1BPP_MSB,T5_VIDEO_FLAG_ONE_IS_BLACK};return true;
}
static uint8_t* back(size_t* n){assert(!stopped);*n=sizeof(pixels);return pixels;}
static bool capacity(){assert(!stopped);return canSubmit && millis()-lastSubmit>=10;}
static bool submit(uint16_t,uint16_t){assert(!stopped);lastSubmit=millis();return true;}
static bool pending(){return false;}
static uint32_t frames(){return 0;}
static void stop(){stopped=true;++stops;}
static const t5_video_api_v1 video={T5_VIDEO_API_VERSION,sizeof(t5_video_api_v1),startVideo,back,capacity,submit,pending,frames,stop,nullptr,nullptr};
extern "C" const t5_video_api_v1* t5_video_get_api(uint32_t){return &video;}
extern "C" int native_hardware_takeover_begin(uint32_t flags){assert(flags==(T5_HARDWARE_TAKEOVER_DISPLAY|T5_HARDWARE_TAKEOVER_UI_VIDEO));return ESP_OK;}
extern "C" int native_hardware_takeover_end(uint32_t){++ends;return failEnd?1:0;}
void nativeTouchDiscardGestures(){}
#include "boot_loading_source.inc"
static void join(){if(worker.joinable())worker.join();}
int main(){
 GfxRenderer renderer;
#if defined(BOARD_T5S3_PRO)
 using namespace StartupScreen;
 // Render all animation phases into a guarded buffer, including complete fade.
 videoSurface={960,540,120,T5_VIDEO_PIXEL_MONO_1BPP_MSB,T5_VIDEO_FLAG_ONE_IS_BLACK};
 uint8_t guarded[sizeof(pixels)+2];guarded[0]=0xa5;guarded[sizeof(guarded)-1]=0x5a;
 for(unsigned t=0;t<3600;t+=40){
   visualTimeMs=t;memset(guarded+1,0,sizeof(pixels));
   drawVideoLogo(guarded+1,sizeof(pixels),9,64,64);
   assert(guarded[0]==0xa5 && guarded[sizeof(guarded)-1]==0x5a);
   memset(guarded+1,0,sizeof(pixels));
   drawVideoLogo(guarded+1,sizeof(pixels),static_cast<uint8_t>((t/40)%10),0,0);
   for(unsigned i=1;i<=sizeof(pixels);++i)assert(guarded[i]==0);
 }
 memset(guarded+1,0,sizeof(pixels));drawVideoLogo(guarded+1,sizeof(pixels),9,0,0);
 for(unsigned i=1;i<=sizeof(pixels);++i)assert(guarded[i]==0);
 // Every settled plate must exactly reproduce the original logo silhouette.
 memset(pixels,0,sizeof(pixels));memset(guarded+1,0,sizeof(pixels));
 visualTimeMs=1200;drawVideoLogo(pixels,sizeof(pixels),9,64,0);
 for(const auto& rect:kLogoRects){
   drawDitheredRoundedRect(guarded+1,sizeof(pixels),150+rect.x*2,320+rect.y*2,
                           rect.width*2,rect.height*2,4,64);
 }
 assert(memcmp(pixels,guarded+1,sizeof(pixels))==0);
 // The final wordmark must exactly match all glyph masks, including the i
 // stem rows that previously travelled incorrectly with the falling dot.
 memset(pixels,0,sizeof(pixels));memset(guarded+1,0,sizeof(pixels));
 visualTimeMs=1800;drawBootWordmark(pixels,sizeof(pixels),270,564,64);
 int wordX=190;
 for(int letter=0;letter<7;++letter){
   for(int y=0;y<32;++y)for(int x=0;x<kWordmarkWidths[letter];++x)
     if(kWordmarkRows[letter][y]&(1u<<x))
       setPhysicalPixel(guarded+1,sizeof(pixels),wordX+x,564+y,true);
   wordX+=kWordmarkWidths[letter];
 }
 assert(memcmp(pixels,guarded+1,sizeof(pixels))==0);
 auto began=millis();boot(renderer);
 assert(millis()-began<200); // reveal is not on the startup caller
 unsigned loadingSteps=0;
 for(unsigned i=0;i<4;++i){++loadingSteps;delay(5);}
 assert(loadingSteps==4);
 assert(finishBoot(renderer));join();
 assert(millis()-began<500); // ready before first block: no reveal/minimum pulse wait
 assert(!videoTakeoverActive && !fadePending);
 assert(isLoading());destinationReady();assert(!isLoading());
 auto priorEnds=ends;assert(finishBoot(renderer));assert(ends==priorEnds);
 // The original full reveal still completes when actual initialization takes longer.
 boot(renderer);delay(1400);assert(finishBoot(renderer));join();
 assert(lastVisibleBlocks==9 && lastTextCoverage==64);
 // Stalled worker must acknowledge cancellation before any teardown or reuse.
 deferWorker=true;boot(renderer);auto priorStops=stops;priorEnds=ends;
 assert(!finishBoot(renderer));assert(stops==priorStops && ends==priorEnds);
 assert(fadePending && videoTakeoverActive);
 pulseTask(nullptr);assert(finishBoot(renderer));deferWorker=false;
 // Driver backpressure is bounded, including cancellation during submit waiting.
 canSubmit=false;boot(renderer);delay(70);began=millis();
 assert(finishBoot(renderer));join();assert(millis()-began<700);canSubmit=true;
 // A failed ownership release cannot start the ordinary renderer over live DMA.
 deferWorker=true;boot(renderer);pulseStopRequested=true;pulseTask(nullptr);
 failEnd=true;assert(!finishBoot(renderer));assert(videoTakeoverActive && fadePending);
 failEnd=false;assert(finishBoot(renderer));deferWorker=false;
 // No worker available: safe single-frame renderer fallback, no cosmetic wait.
 failCreate=true;int before=renderer.presents;boot(renderer);
 assert(bootBackend==BootBackend::Renderer && renderer.presents==before+1);
 before=renderer.presents;assert(finishBoot(renderer));assert(renderer.presents==before);
#else
 StartupScreen::boot(renderer);assert(renderer.presents==1);
 assert(StartupScreen::finishBoot(renderer));assert(renderer.presents==1);
#endif
 puts("boot startup overlap, early readiness, cancellation, ownership retry and fallback PASS");
}
