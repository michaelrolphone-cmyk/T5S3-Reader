#include "RiscUsbHidV1.h"
#include "RiscUsbInterruptV1.h"
#include "RiscInputNavigationV1.h"
#include "RiscTextInputV1.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <map>
#include <memory>
#include <string>
#include <HalStorage.h>

// The real owner input adapter uses its existing host clock/storage declarations.
#include "NativeNavigationInput.cpp"
uint32_t fakeTime = 1000;
bool storageReady = true, closeOk = true;
std::map<std::string, std::shared_ptr<TestFile>> files;
HalStorage Storage;
static const risc_input_navigation_api_v1* selectedNavigation;
static unsigned acquisitions, ownerTurns, touches, uiTurns;
static uint32_t lastTouchDelay, maxTouchDelay;
namespace RuntimeInstalledProviders {
bool acquireCapability(const char* capability,uint32_t version,Lease* out) {
    assert(!strcmp(capability,"input.navigation") && version==1 && !out->grant.slot);
    ++acquisitions; *out={{1,1},selectedNavigation}; return true;
}
bool release(Lease* out) { assert(out->grant.slot==1);*out={};return true; }
bool hasLiveGrants(){return false;}
const char* lastError(){return "fixture";}
}
bool drainPlatformProvidersForSleep(){return true;}

static constexpr unsigned MAX_DEVICES=4;
static unsigned attachedCount, failuresRemaining[MAX_DEVICES], controlCalls[MAX_DEVICES];
static bool attached[MAX_DEVICES],claimed[MAX_DEVICES];
static unsigned claims,releases,configs,interruptReads,failedControls,uiKeyPresses;
static uint64_t controlDeadlineMs,modeledControlMs;
static bool modelSlowFailure, modelNoCompletion;
static risc_usb_controller_event_v1 events[32];
static size_t eventCount,eventCursor;
static uint8_t nextUsage[MAX_DEVICES];
static bool haveReport[MAX_DEVICES];

namespace ControllerPath {
using TickType_t=uint32_t;
constexpr int ESP_OK=0,USB_TRANSFER_STATUS_COMPLETED=0,USB_TRANSFER_STATUS_STALL=2;
#define pdMS_TO_TICKS(x) (x)
struct usb_transfer_t{
    void* device_handle;uint8_t bEndpointAddress;int num_bytes,actual_num_bytes,status;
    void (*callback)(usb_transfer_t*);void* context;uint8_t data_buffer[256];
};
struct Device{void* handle;bool attached;};
static Device devices[MAX_DEVICES];
static bool running=true,fault=false,inFlight=false,completed=false;
static usb_transfer_t record{};static usb_transfer_t* transfer=&record;
static void* client=reinterpret_cast<void*>(1);
static uint32_t submittedAt,completeAfter;
static bool failRequest,neverComplete;
static unsigned pumps,submits;
static TickType_t xTaskGetTickCount(){return fakeTime;}
static Device* device(uint64_t id){
    assert(id>=100 && id<100+MAX_DEVICES);unsigned i=(unsigned)(id-100);
    devices[i]={reinterpret_cast<void*>(static_cast<uintptr_t>(id)),::attached[i]};return &devices[i];
}
static int usb_host_transfer_submit_control(void*,usb_transfer_t* t){
    ++submits;submittedAt=fakeTime;
    if(!neverComplete && !completeAfter){t->status=failRequest?USB_TRANSFER_STATUS_STALL:USB_TRANSFER_STATUS_COMPLETED;t->actual_num_bytes=8;t->callback(t);}
    return ESP_OK;
}
static bool pump(TickType_t wait){
    ++pumps;fakeTime+=wait;
    if(inFlight && !neverComplete && fakeTime-submittedAt>=completeAfter){
        transfer->status=failRequest?USB_TRANSFER_STATUS_STALL:USB_TRANSFER_STATUS_COMPLETED;
        transfer->actual_num_bytes=8;transfer->callback(transfer);
    }
    return true;
}
static bool drain_bulk(bool){assert(false && "Control endpoint is not bulk");return false;}
// Actual controller complete_transfer, wait_completion, idle_transfer and
// control bodies are inserted here unchanged by the portable runner.
CONTROLLER_FUNCTIONS
}
static const uint8_t configurationDescriptor[]={
    9,2,34,0,1,1,0,0x80,50,
    9,4,0,0,1,3,1,1,0,
    9,0x21,0x11,0x01,0,1,0x22,63,0,
    7,5,0x81,3,8,0,10
};
static unsigned deviceIndex(uint64_t physical){assert(physical>=100 && physical<100+MAX_DEVICES);return (unsigned)(physical-100);}
static int32_t nextEvent(void*,risc_usb_controller_event_v1* out){
    if(eventCursor==eventCount)return 0;*out=events[eventCursor++];return 1;
}
static bool config(void*,uint64_t physical,uint8_t* bytes,size_t* len,uint16_t* vid,uint16_t* pid){
    const unsigned i=deviceIndex(physical);assert(attached[i] && *len>=sizeof(configurationDescriptor));
    ++configs;memcpy(bytes,configurationDescriptor,sizeof(configurationDescriptor));
    *len=sizeof(configurationDescriptor);*vid=0x1234;*pid=(uint16_t)(0x1000+i);return true;
}
static bool claim(void*,uint64_t physical,uint8_t iface,uint8_t alt,uint64_t* out){
    unsigned i=deviceIndex(physical);assert(attached[i] && !claimed[i] && !iface && !alt);
    claimed[i]=true;++claims;*out=physical+1000;return true;
}
static bool releaseClaim(void*,uint64_t token){
    unsigned i=deviceIndex(token-1000);assert(claimed[i]);
    if(ControllerPath::inFlight)return false;
    claimed[i]=false;++releases;return true;
}
static int32_t control(void*,uint64_t physical,uint8_t type,uint8_t request,uint16_t value,uint16_t iface,uint8_t* payload,uint16_t len,uint32_t timeout){
    unsigned i=deviceIndex(physical);assert(attached[i] && claimed[i]);
    assert(type==0x21 && request==0x0b && !value && !iface && !payload && !len && timeout==100);
    ++controlCalls[i];controlDeadlineMs+=timeout;
    const bool failure=failuresRemaining[i]!=0;
    if(failure){--failuresRemaining[i];++failedControls;}
    ControllerPath::failRequest=failure;
    ControllerPath::completeAfter=failure && modelSlowFailure ? 60 : 0;
    ControllerPath::neverComplete=modelNoCompletion;
    const auto began=fakeTime;
    const int32_t rc=ControllerPath::control(nullptr,physical,type,request,value,iface,payload,len,timeout);
    modeledControlMs+=fakeTime-began;
    assert((rc==0)==!failure);
    if(!modelNoCompletion)assert(!ControllerPath::inFlight);
    return rc;
}
static int32_t bulkRead(void*,uint64_t,uint8_t,uint8_t*,size_t,uint32_t){return -1;}
static int32_t bulkWrite(void*,uint64_t,uint8_t,const uint8_t*,size_t,uint32_t){return -1;}
static int32_t readInterrupt(void*,uint64_t token,uint8_t ep,uint8_t* out,size_t cap,uint32_t timeout){
    unsigned i=deviceIndex(token-1000);assert(attached[i] && claimed[i] && ep==0x81 && cap>=8 && timeout==10);
    ++interruptReads;
    if(!haveReport[i])return 0;
    memset(out,0,8);out[2]=nextUsage[i];haveReport[i]=false;return 8;
}
static bool quiesceController(void*){
    for(bool b:claimed)if(b)return false;return true;
}
static const risc_usb_controller_interrupt_v1 controller={
    {1,sizeof(controller),nullptr,nextEvent,config,claim,releaseClaim,control,bulkRead,bulkWrite,quiesceController},readInterrupt
};
static bool padPoll(void*,size_t n){assert(n==4);return true;}
static bool padSnapshot(void*,risc_usb_gamepad_state_v1*,size_t* n){*n=0;return true;}
static const risc_usb_gamepad_api_v1 emptyPad={1,sizeof(emptyPad),nullptr,nullptr,nullptr,padPoll,nullptr,padSnapshot};
namespace EditorConsumer {
static const risc_usb_keyboard_api_v1* keyboard;
static uint64_t subscription;
static struct {uint8_t kind,usage,modifiers;} pending_keys[256];
static size_t pending_head,pending_count;
static bool pending_gap,pending_fault;
EDITOR_COLLECT_FUNCTION
}
static const risc_driver_v2* load(const char* path,void** module){
    *module=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!*module){fprintf(stderr,"dlopen: %s\n",dlerror());abort();}
    auto get=reinterpret_cast<risc_driver_get_v2_fn>(dlsym(*module,"t5_driver_get"));
    assert(get);auto* driver=get(2);assert(driver && driver->quiesce);return driver;
}

// This body is inserted verbatim from MappedInputManager.cpp by the runner.
static uint32_t turnStarted;
struct Gpio{void update(){}};
struct MappedInputManager{mutable bool navigationHomeConsumed=false;Gpio gpio;void update()const;};
static struct {uint8_t externalInputNavigation=1;} SETTINGS;
static void nativeDeviceDiscoveryTick(){}
static void nativeTouchTick(){++touches;lastTouchDelay=fakeTime-turnStarted;if(lastTouchDelay>maxTouchDelay)maxTouchDelay=lastTouchDelay;}

static void turn(MappedInputManager& input,uint32_t advance=20){
    fakeTime+=advance;turnStarted=fakeTime;++ownerTurns;input.update();++uiTurns;
    if(nativeNavigationFrame().pressed & RISC_NAV_CONFIRM)++uiKeyPresses;
}
static void attach(unsigned i){assert(!attached[i] && eventCount<32);attached[i]=true;++attachedCount;events[eventCount++]={1,100+i};}
static void detach(unsigned i){assert(attached[i] && eventCount<32);attached[i]=false;--attachedCount;events[eventCount++]={2,100+i};}
static unsigned controls(){unsigned n=0;for(auto x:controlCalls)n+=x;return n;}
static void output(const char* scenario){
    printf("{\"scenario\":\"%s\",\"owner_turns\":%u,\"ui_turns\":%u,\"touch_ticks\":%u,\"acquisitions\":%u,\"claims\":%u,\"releases\":%u,\"protocol_calls\":%u,\"failed_protocol_calls\":%u,\"requested_control_deadline_ms\":%llu,\"modeled_control_elapsed_ms\":%llu,\"max_owner_to_touch_model_ms\":%u,\"config_copies\":%u,\"interrupt_reads\":%u,\"ui_key_presses\":%u}\n",
        scenario,ownerTurns,uiTurns,touches,acquisitions,claims,releases,controls(),failedControls,
        (unsigned long long)controlDeadlineMs,(unsigned long long)modeledControlMs,maxTouchDelay,configs,interruptReads,uiKeyPresses);
}
int main(int argc,char** argv){
    assert(argc==7);const std::string scenario=argv[6];
    void* modules[5]{};const risc_driver_v2* d[5]{};
    for(unsigned i=0;i<5;++i)d[i]=load(argv[i+1],&modules[i]);
    risc_provider_dependency_v1 deps[]={
        {"usb.controller",1,&controller},{"usb.host",1,d[0]->capability},{"usb.hid",1,d[1]->capability},
        {"usb.hid.keyboard",1,d[2]->capability}
    };
    for(unsigned i=0;i<4;++i)assert(d[i]->start(&deps[i],1));
    risc_provider_dependency_v1 navDeps[]={{"input.text",1,d[3]->capability},{"usb.hid.gamepad",1,&emptyPad},{"usb.xinput.gamepad",1,&emptyPad}};
    assert(d[4]->start(navDeps,3));selectedNavigation=static_cast<const risc_input_navigation_api_v1*>(d[4]->capability);
    const bool directEditor=scenario=="text-editor-direct";
    MappedInputManager input;
    if(!directEditor){turn(input);assert(acquisitions==1 && !controls());} // Acquire retained API; neutral subscription rearm.
    else {EditorConsumer::keyboard=static_cast<const risc_usb_keyboard_api_v1*>(d[2]->capability);EditorConsumer::subscription=EditorConsumer::keyboard->subscribe(nullptr,0);assert(EditorConsumer::subscription);}
    unsigned repeats=1000;
    if(scenario=="absent"){}
    else if(scenario=="healthy"){attach(0);}
    else if(scenario=="transient"){attach(0);failuresRemaining[0]=1;modelSlowFailure=true;}
    else if(scenario=="persistent-immediate"){attach(0);failuresRemaining[0]=100000;}
    else if(scenario=="persistent-slow-failure"){attach(0);failuresRemaining[0]=100000;modelSlowFailure=true;}
    else if(scenario=="two-failed"){attach(0);attach(1);failuresRemaining[0]=failuresRemaining[1]=100000;modelSlowFailure=true;}
    else if(scenario=="failed-plus-healthy"){attach(0);attach(1);failuresRemaining[0]=100000;modelSlowFailure=true;}
    else if(scenario=="recovery"){attach(0);failuresRemaining[0]=100;modelSlowFailure=true;}
    else if(scenario=="undrained-timeout"){attach(0);failuresRemaining[0]=100000;modelNoCompletion=true;}
    else if(directEditor){attach(0);failuresRemaining[0]=100000;modelSlowFailure=true;}
    else if(scenario=="reconnect"){attach(0);}
    else assert(false);
    for(unsigned i=0;i<repeats;++i){
        if(scenario=="failed-plus-healthy" && i==3){haveReport[1]=true;nextUsage[1]=40;}
        if(scenario=="failed-plus-healthy" && i==4){haveReport[1]=true;nextUsage[1]=0;}
        if(directEditor){fakeTime+=20;turnStarted=fakeTime;++ownerTurns;EditorConsumer::collect_keyboard(nullptr);nativeTouchTick();++uiTurns;}
        else turn(input);
    }
    if(scenario=="absent")assert(controls()==0 && maxTouchDelay==0);
    if(scenario=="healthy")assert(controls()==1 && claims==1 && maxTouchDelay==0);
    if(scenario=="transient")assert(controls()==2 && failedControls==1 && maxTouchDelay==60);
    if(scenario=="persistent-immediate")assert(controls()==1000 && claims==1000 && releases==1000 && maxTouchDelay==0);
    if(scenario=="persistent-slow-failure")assert(controls()==1000 && claims==1000 && releases==1000 && maxTouchDelay==60);
    if(scenario=="two-failed")assert(controls()==2000 && claims==2000 && releases==2000 && maxTouchDelay==120);
    if(scenario=="failed-plus-healthy")assert(controls()==1001 && controlCalls[1]==1 && uiKeyPresses==1 && maxTouchDelay==60);
    if(scenario=="recovery")assert(controls()==101 && failedControls==100 && releases==100 && maxTouchDelay==60);
    if(scenario=="undrained-timeout")assert(controls()==1 && claims==1 && releases==0 && maxTouchDelay==100 && ControllerPath::inFlight);
    if(directEditor)assert(controls()==1000 && failedControls==1000 && maxTouchDelay==60 && !EditorConsumer::pending_fault && !EditorConsumer::pending_gap && !EditorConsumer::pending_count);
    if(scenario=="reconnect"){
        assert(controls()==1);detach(0);turn(input);attach(0);turn(input);assert(controls()==2 && claims==2 && releases==1);
    }
    output(scenario.c_str());
    assert(acquisitions==(directEditor?0u:1u) && touches==ownerTurns && uiTurns==ownerTurns);
    if(directEditor)assert(EditorConsumer::keyboard->unsubscribe(nullptr,EditorConsumer::subscription));
    if(modelNoCompletion){ControllerPath::transfer->status=ControllerPath::USB_TRANSFER_STATUS_STALL;ControllerPath::complete_transfer(ControllerPath::transfer);}
    for(unsigned i=0;i<MAX_DEVICES;++i)if(attached[i])detach(i);
    if(!directEditor){turn(input);assert(nativeNavigationSuspend());}
    for(unsigned i=5;i>0;--i){assert(d[i-1]->quiesce());d[i-1]->stop();assert(dlclose(modules[i-1])==0);}
    assert(claims==releases && quiesceController(nullptr));
    return 0;
}
