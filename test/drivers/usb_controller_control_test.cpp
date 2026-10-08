#include "OwnedControlRequest.h"
#include "OwnedBulkRequest.h"
#include "OwnedAdmissionRequest.h"
#include "usb_control_rig.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using RiscUsbController::OwnedControlRequest;
namespace Controller {
struct Device;
static RiscUsbController::OwnedBulkRequest ownedBulk;
static RiscUsbController::OwnedAdmissionRequest ownedAdmission;
static OwnedControlRequest ownedControl;
static bool fault,running=true,inFlight,completed;
static uint64_t serial=10,clock=100;
static usb_transfer_t *transfer;
static usb_host_client_handle_t client;
/* Actual Device definition precedes the extracted production routines. */
struct OwnedBulkPort { void *unused; uint64_t now() const { return clock; } };
extern Device devices[1];
#include "control_wrapper.inc"
Device devices[1];
static void init() { transfer=rig_transfer(); client=rig_client(); devices[0]={rig_device(),9,0,0,true,false}; }
}
struct Port {
    uint64_t time=100;
    uint64_t afterSubmit=0,afterPoll=0,afterCancel=0;
    uint64_t now() { return time; }
    esp_err_t submit(usb_transfer_t *p,uint64_t id) { auto rc=risc_usb_control_submit(rig_client(),p,id); if(afterSubmit) time=afterSubmit; return rc; }
    esp_err_t poll(uint64_t id) { auto rc=risc_usb_control_poll(id); if(afterPoll) time=afterPoll; return rc; }
    esp_err_t cancel(uint64_t id) { auto rc=risc_usb_control_cancel(id); if(afterCancel) time=afterCancel; return rc; }
};
static OwnedControlRequest *dispatchRequest;
static Port *dispatchPort;
static unsigned dispatchMode,dispatchCalls;
static uint32_t dispatchCookie;
static constexpr uint64_t ID=0x100000001ull;
static void dispatch_progress(unsigned after) {
    ++dispatchCalls;
    if(dispatchMode==3) {
        if(after) control_pool_drain(700,0);
        assert(risc_usb_claim_release_step(700)==ESP_ERR_NOT_FINISHED);
        risc_usb_claim_state state{};
        assert(risc_usb_claim_state_copy(700,&state) && state.acquired==1 && state.published);
        assert(admission_channels()==1 && admission_endpoint_cookie(700,0)==dispatchCookie);
        assert(risc_usb_claim_begin(rig_client(),rig_device(),0,0,701)==ESP_ERR_NOT_FINISHED);
        return;
    }
    assert(control_dispatch_count()>0 && !control_callback_restored());
    if(dispatchMode==4) dispatchPort->time=200;
    int expected=(dispatchMode==4 || (after && dispatchMode==2))?RISC_STREAM_RETAINED:RISC_STREAM_AGAIN;
    for(unsigned i=0;i<3;++i) assert(dispatchRequest->step(*dispatchPort,ID)==expected);
    assert(control_count()==1 && rig_callback_custody() && !control_callback_restored());
}
static uint8_t outgoing[4096], incoming[4096];
static void quiet() { assert(control_logs_count()==0 && control_notifications_count()==0); }
static int begin(OwnedControlRequest &r,Port &p,bool in=true,unsigned size=17,unsigned budget=100) {
    std::memset(outgoing,0x34,sizeof(outgoing));
    return r.begin(p,rig_transfer(),rig_device(),ID,in?0xc0:0x40,0xa5,0x1234,0x5678,outgoing,(uint16_t)size,budget);
}
static void submit(OwnedControlRequest &r,Port &p) {
    assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_count()==1);
    assert(rig_dma_active() && rig_callback_custody() && !control_callback_restored()); quiet();
}
static void success_irqs(unsigned size) {
    assert(control_stage()==0);
    control_irq(0,0);
    assert(control_pid()==1);
    if(size) { assert(control_stage()==1); control_irq(0,(int)size); }
    assert(control_stage()==2); control_irq(0,0);
    assert(!rig_dma_active()); quiet();
}
static void retire(OwnedControlRequest &r,Port &p,int expected) {
    assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_retired());
    assert(control_count()==1 && rig_callback_custody() && !r.callback_seen());
    assert(r.step(p,ID)==expected && r.callback_seen());
    assert(control_count()==0 && !rig_callback_custody() && control_callback_restored());
    assert(!risc_usb_control_busy()); quiet();
}
static void retained(OwnedControlRequest &r,Port &p) {
    assert(r.state()==OwnedControlRequest::State::Retained);
    assert(r.step(p,ID)==RISC_STREAM_RETAINED);
    r.cancel(ID); p.time=101;
    assert(r.step(p,ID)==RISC_STREAM_RETAINED);
    assert(r.take(ID,incoming,sizeof(incoming))==RISC_STREAM_RETAINED);
    assert(begin(r,p)==RISC_STREAM_RETAINED && r.owns_storage());
    assert(risc_usb_control_busy()); quiet();
}
static void cancel_ack(OwnedControlRequest &r,Port &p,int reason=RISC_STREAM_CANCELLED) {
    assert(r.step(p,ID)==RISC_STREAM_AGAIN && rig_halts()==1);
    assert(rig_dma_active() && rig_halt_owned());
    control_irq(0,0); assert(!rig_dma_active());
    assert(r.step(p,ID)==RISC_STREAM_AGAIN && !rig_halt_owned());
    retire(r,p,reason);
}
int main(int argc,char **argv) {
    assert(argc==2); const char *name=argv[1]; control_init(); OwnedControlRequest r; Port p;
    const auto is=[&](const char *value) { return std::strcmp(name,value)==0; };
    if(is("in") || is("out") || is("zero") || is("short") || is("maximum")) {
    bool in=std::strcmp(name,"out")!=0;
    unsigned size=is("zero")?0:is("maximum")?4096:17;
    assert(begin(r,p,in,size)==RISC_STREAM_AGAIN);
    const uint8_t setup[]={static_cast<uint8_t>(in?0xc0:0x40),0xa5,0x34,0x12,0x78,0x56,static_cast<uint8_t>(size),static_cast<uint8_t>(size>>8)};
    assert(std::memcmp(rig_transfer()->data_buffer,setup,8)==0);
    std::memset(outgoing,0xef,sizeof(outgoing));
    if(!in) assert(rig_transfer()->data_buffer[8]==0x34);
    submit(r,p);
    unsigned actual=is("short")?3:size;
    success_irqs(actual); retire(r,p,(int)actual);
    if(in && size) {
        assert(r.take(ID,nullptr,0)==RISC_STREAM_LIMIT);
        assert(r.take(ID,incoming,actual-1)==RISC_STREAM_LIMIT);
    }
    assert(r.take(ID,incoming,sizeof(incoming))==(int)actual);
    if(in && size) assert(incoming[0]==0x6b && incoming[actual-1]==0x6b);
    assert(!r.owns_storage() && !rig_transfer()->callback);
    } else if(is("cancel_unsubmitted") || is("deadline_unsubmitted")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN);
        if(is("cancel_unsubmitted")) r.cancel(ID); else p.time=200;
        int reason=is("cancel_unsubmitted")?RISC_STREAM_CANCELLED:RISC_STREAM_TIMEOUT;
        assert(r.step(p,ID)==reason && control_count()==0 && !rig_dma_active());
        assert(r.take(ID,nullptr,0)==reason && control_callback_restored());
    } else if(!std::strncmp(name,"cancel_stage_",13) || is("timeout_ack")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p);
        unsigned stage=is("timeout_ack")?0:(unsigned)std::atoi(name+13);
        for(unsigned i=0;i<stage;++i) control_irq(0,i==1?17:0);
        if(is("timeout_ack")) p.time=175; else r.cancel(ID);
        int reason=is("timeout_ack")?RISC_STREAM_TIMEOUT:RISC_STREAM_CANCELLED;
        cancel_ack(r,p,reason); assert(r.take(ID,nullptr,0)==reason);
    } else if(is("cancel_completed") || is("cancel_retired")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); success_irqs(17);
        if(is("cancel_retired")) assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_retired());
        r.cancel(ID); assert(r.step(p,ID)==RISC_STREAM_AGAIN);
        if(is("cancel_completed")) retire(r,p,RISC_STREAM_CANCELLED);
        else assert(r.step(p,ID)==RISC_STREAM_CANCELLED && control_count()==0);
        assert(r.take(ID,nullptr,0)==RISC_STREAM_CANCELLED); quiet();
    } else if(!std::strncmp(name,"error_",6)) {
        unsigned event=(unsigned)std::atoi(name+6);
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); control_irq(event,0);
        retire(r,p,RISC_STREAM_IO); assert(r.take(ID,nullptr,0)==RISC_STREAM_IO);
    } else if(!std::strncmp(name,"cancel_error_",13)) {
        unsigned event=(unsigned)std::atoi(name+13);
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); r.cancel(ID);
        assert(r.step(p,ID)==RISC_STREAM_AGAIN && rig_halt_owned());
        control_irq(event,0);
        assert(r.step(p,ID)==RISC_STREAM_AGAIN && !rig_halt_owned());
        retire(r,p,RISC_STREAM_CANCELLED);
    } else if(is("missing_halt") || is("late_halt") || is("detach_before") || is("detach_after")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); r.cancel(ID);
        if(is("detach_before")) rig_disconnect();
        assert(r.step(p,ID)==RISC_STREAM_AGAIN);
        if(is("detach_after")) { control_irq(0,0); rig_disconnect(); }
        p.time=200; assert(r.step(p,ID)==RISC_STREAM_RETAINED);
        if(is("late_halt")) control_irq(0,0);
        assert(control_count()==1 && rig_callback_custody() && !control_callback_restored()); retained(r,p);
    } else if(is("detached_clear")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); r.cancel(ID);
        assert(r.step(p,ID)==RISC_STREAM_AGAIN); control_irq(0,0); rig_disconnect();
        assert(r.step(p,ID)==RISC_STREAM_AGAIN);
        assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_retired());
        assert(r.step(p,ID)==RISC_STREAM_RETAINED && control_count()==1); retained(r,p);
    } else if(is("deadline_retired") || is("deadline_submit") || is("deadline_poll") || is("deadline_restore") || is("deadline_cancel")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN);
        if(is("deadline_submit")) { p.afterSubmit=200; assert(r.step(p,ID)==RISC_STREAM_RETAINED); }
        else {
            submit(r,p);
            if(is("deadline_cancel")) { r.cancel(ID); p.afterCancel=200; assert(r.step(p,ID)==RISC_STREAM_RETAINED); }
            else {
                success_irqs(17);
                if(is("deadline_poll")) p.afterPoll=200;
                int rc=r.step(p,ID);
                if(is("deadline_poll")) assert(rc==RISC_STREAM_RETAINED);
                else {
                    assert(rc==RISC_STREAM_AGAIN && control_retired());
                    if(is("deadline_restore")) {
                        p.afterPoll=200;
                        assert(r.step(p,ID)==RISC_STREAM_TIMEOUT && control_count()==0 && r.callback_seen());
                        assert(r.take(ID,nullptr,0)==RISC_STREAM_TIMEOUT);
                        std::printf("control %s PASS\n",name); return 0;
                    }
                    p.time=200; assert(r.step(p,ID)==RISC_STREAM_RETAINED);
                }
            }
        }
        assert(control_count()==1 && rig_callback_custody()); retained(r,p);
    } else if(is("scope")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p);
        assert(r.step(p,ID+1)==RISC_STREAM_INVALID && r.take(ID+1,nullptr,0)==RISC_STREAM_INVALID);
        r.cancel(ID+1); assert(r.step(p,ID)==RISC_STREAM_AGAIN && rig_halts()==0);
        assert(risc_usb_control_cancel(ID+1)==ESP_ERR_INVALID_STATE);
        assert(risc_usb_control_poll(ID+1)==ESP_ERR_INVALID_STATE && control_count()==1);
        success_irqs(17); retire(r,p,17);
        assert(r.take(ID+1,incoming,sizeof(incoming))==RISC_STREAM_INVALID && r.owns_storage());
        assert(r.take(ID,incoming,sizeof(incoming))==17);
    } else if(is("stale_callback") || is("duplicate_callback") || is("retired_callback") || is("finished_callback") || is("reuse_callback")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); uint32_t cookie=control_cookie();
        if(is("stale_callback")) control_saved_callback(cookie+1);
        else {
            success_irqs(17);
            if(is("retired_callback")) assert(r.step(p,ID)==RISC_STREAM_AGAIN);
            if(is("finished_callback") || is("reuse_callback")) retire(r,p,17);
            if(is("reuse_callback")) {
                assert(r.take(ID,incoming,sizeof(incoming))==17);
                assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p);
                assert(control_cookie()!=cookie);
            }
            control_saved_callback(cookie);
        }
        assert(r.step(p,ID)==RISC_STREAM_RETAINED); retained(r,p);
    } else if(!std::strncmp(name,"restore_fault_",14) || !std::strncmp(name,"retire_fault_",13)) {
        bool restore=!std::strncmp(name,"restore_fault_",14);
        unsigned fault=(unsigned)std::atoi(name+(restore?14:13));
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); success_irqs(17);
        if(restore) assert(r.step(p,ID)==RISC_STREAM_AGAIN);
        control_fault(fault,true);
        assert(r.step(p,ID)==RISC_STREAM_RETAINED);
        control_fault(fault,false); retained(r,p);
        assert(control_count()==1 && rig_callback_custody() && !control_callback_restored());
    } else if(!std::strncmp(name,"acquire_fault_",14)) {
        unsigned fault=(unsigned)std::atoi(name+14);
        assert(begin(r,p)==RISC_STREAM_AGAIN); control_fault(fault,true);
        assert(r.step(p,ID)==RISC_STREAM_IO && control_count()==0 && !rig_callback_custody());
        assert(!rig_dma_active() && !risc_usb_control_busy());
        assert(r.take(ID,nullptr,0)==RISC_STREAM_IO);
    } else if(!std::strncmp(name,"lock_",5)) {
        unsigned lock=(unsigned)std::atoi(name+5);
        assert(begin(r,p)==RISC_STREAM_AGAIN); rig_lock(lock,true);
        assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_count()==0 && !rig_dma_active());
        rig_lock(lock,false); submit(r,p); success_irqs(17);
        rig_lock(lock,true); assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_count()==1);
        rig_lock(lock,false); assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_retired());
        rig_lock(lock,true); assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_count()==1);
        rig_lock(lock,false); assert(r.step(p,ID)==17 && control_count()==0);
    } else if(is("missing_library") || is("deferred_library") || is("deferred_client")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN);
        if(is("missing_library")) rig_library_missing();
        if(is("deferred_library")) rig_library_pending(3);
        if(is("deferred_client")) rig_queue_events(1,false);
        if(is("missing_library")) assert(r.step(p,ID)==RISC_STREAM_IO);
        else for(unsigned i=0;i<4;++i) assert(r.step(p,ID)==RISC_STREAM_AGAIN);
        assert(!rig_dma_active() && control_count()==0 && !risc_usb_control_busy());
        int reason=is("missing_library")?RISC_STREAM_IO:RISC_STREAM_TIMEOUT;
        p.time=200; assert(r.step(p,ID)==reason);
        assert(r.take(ID,nullptr,0)==reason);
    } else if(is("legacy_guard")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p);
        assert(control_legacy_submit()==ESP_ERR_INVALID_STATE && control_count()==1);
        control_legacy_updates(true);
        assert(rig_legacy_command()==ESP_ERR_INVALID_STATE && rig_legacy_free()==ESP_ERR_INVALID_STATE);
        success_irqs(17); assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_retired());
        assert(control_legacy_submit()==ESP_ERR_INVALID_STATE);
        control_legacy_updates(true);
        assert(rig_legacy_command()==ESP_ERR_INVALID_STATE && rig_legacy_free()==ESP_ERR_INVALID_STATE);
        assert(r.step(p,ID)==17 && control_callback_restored()); quiet();
        control_legacy_updates(false);
        control_legacy_notify(); assert(control_logs_count()==1 && control_notifications_count()==1);
    } else if(is("close_guard")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p);
        bool closed=false,deferred=false;
        assert(risc_usb_device_close_try(rig_client(),rig_device(),&closed,&deferred)==ESP_ERR_INVALID_STATE);
        assert(!closed && admission_device_refs()==1 && admission_device_opened());
        success_irqs(17); retire(r,p,17);
        assert(risc_usb_device_close_try(rig_client(),rig_device(),&closed,&deferred)==ESP_OK && closed && !deferred);
        assert(admission_device_refs()==0);
    } else if(is("clock") || is("overflow")) {
        if(is("overflow")) { p.time=UINT64_MAX-50; assert(begin(r,p)==RISC_STREAM_IO && !r.owns_storage()); }
        else { assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); p.time=99; assert(r.step(p,ID)==RISC_STREAM_RETAINED); retained(r,p); }
    } else if(is("invalid")) {
        assert(begin(r,p,true,4097)==RISC_STREAM_INVALID);
        assert(begin(r,p,true,17,0)==RISC_STREAM_INVALID);
        assert(begin(r,p,true,17,1001)==RISC_STREAM_INVALID);
        assert(r.begin(p,rig_transfer(),rig_device(),ID,0,0,0,0,nullptr,1,100)==RISC_STREAM_INVALID);
        assert(r.begin(p,rig_transfer(),rig_device(),0,0,0,0,0,nullptr,0,100)==RISC_STREAM_INVALID);
        assert(!r.owns_storage() && control_count()==0);
    } else if(is("pending_cancel")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); rig_pending_transfer();
        r.cancel(ID); assert(r.step(p,ID)==RISC_STREAM_AGAIN && !rig_dma_active());
        retire(r,p,RISC_STREAM_CANCELLED);
    } else if(is("deferred_callback") || is("dispatch_duplicate") || is("dispatch_error") ||
              is("dispatch_retired") || is("dispatch_deadline") || is("dispatch_exhaustion")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p);
        dispatchRequest=&r; dispatchPort=&p;
        dispatchMode=is("dispatch_deadline")?4:0;
        if(is("deferred_callback") || is("dispatch_deadline")) control_dispatch_hook(dispatch_progress);
        if(is("dispatch_exhaustion")) control_dispatch_exhaust();
        success_irqs(17);
        if(is("dispatch_duplicate") || is("dispatch_error") || is("dispatch_retired")) {
            if(is("dispatch_retired")) assert(r.step(p,ID)==RISC_STREAM_AGAIN && control_retired());
            dispatchMode=2; control_dispatch_hook(dispatch_progress);
            control_dispatch_decoded(is("dispatch_error")?1:0);
        }
        control_dispatch_hook(nullptr);
        if(is("deferred_callback")) {
            assert(dispatchCalls==2 && !control_retired() && control_dispatch_count()==0);
            retire(r,p,17);
        } else {
            if(is("dispatch_exhaustion")) {
                assert(control_dispatch_count()==UINT32_MAX && r.step(p,ID)==RISC_STREAM_AGAIN);
                p.time=200;
            } else assert(dispatchCalls==2 && control_dispatch_count()==0);
            assert(r.step(p,ID)==RISC_STREAM_RETAINED); retained(r,p);
        }
    } else if(is("pool_dispatch")) {
        admission_config(1,1); assert(admission_prepare(0)==ESP_OK);
        assert(risc_usb_claim_begin(rig_client(),rig_device(),0,0,700)==ESP_OK);
        unsigned steps=0; esp_err_t rc;
        do { rc=risc_usb_claim_step(700); assert(++steps<32); } while(rc==ESP_ERR_NOT_FINISHED);
        assert(rc==ESP_OK && admission_channels()==1);
        dispatchCookie=admission_endpoint_cookie(700,0); dispatchMode=3;
        control_dispatch_hook(dispatch_progress); control_pool_dispatch(700,0); control_dispatch_hook(nullptr);
        assert(dispatchCalls==2);
        assert(risc_usb_claim_release_step(700)==ESP_ERR_NOT_FINISHED && !admission_channels());
        assert(risc_usb_claim_release_step(700)==ESP_OK);
        assert(risc_usb_claim_begin(rig_client(),rig_device(),0,0,701)==ESP_OK);
        steps=0; do { rc=risc_usb_claim_step(701); assert(++steps<32); } while(rc==ESP_ERR_NOT_FINISHED);
        assert(rc==ESP_OK && admission_endpoint_cookie(701,0)!=dispatchCookie);
        admission_saved_callback(dispatchCookie);
        assert(risc_usb_admission_faulted() && risc_usb_claim_release_step(701)==ESP_ERR_INVALID_STATE);
    } else if(is("cookie_exhaustion") || is("malformed_setup")) {
        assert(begin(r,p)==RISC_STREAM_AGAIN);
        if(is("cookie_exhaustion")) control_exhaust_cookies();
        else rig_transfer()->data_buffer[6]=18;
        assert(r.step(p,ID)==RISC_STREAM_IO && control_count()==0 && !risc_usb_control_busy());
        assert(control_callback_restored() && !rig_callback_custody());
    } else if(is("reuse")) {
        for(unsigned i=0;i<5;++i) {
            assert(begin(r,p)==RISC_STREAM_AGAIN); submit(r,p); success_irqs(17); retire(r,p,17);
            assert(r.take(ID,incoming,sizeof(incoming))==17);
        }
    } else if(is("wrapper") || is("wrapper_retained") || is("wrapper_tokens")) {
        using namespace Controller; init(); uint64_t operation=99;
        assert(begin_owned_control(8,0x80,1,0,0,nullptr,17,100,&operation)==RISC_STREAM_INVALID && operation==0);
        assert(begin_owned_control(9,0x80,1,0,0,nullptr,17,100,&operation)==RISC_STREAM_AGAIN);
        assert(operation==11 && native_admission_guard());
        uint64_t second=0;
        assert(begin_owned_control(9,0x80,1,0,0,nullptr,17,100,&second)==RISC_STREAM_BUSY && second==0);
        assert(step_owned_control(operation)==RISC_STREAM_AGAIN && control_count()==1);
        if(is("wrapper_retained")) {
            Controller::clock=200; assert(step_owned_control(operation)==RISC_STREAM_RETAINED);
            assert(begin_owned_control(9,0x80,1,0,0,nullptr,17,100,&second)==RISC_STREAM_RETAINED && second==0);
            assert(take_owned_control(operation,incoming,sizeof(incoming))==RISC_STREAM_RETAINED && native_admission_guard());
        } else {
            assert(step_owned_control(operation+1)==RISC_STREAM_INVALID); cancel_owned_control(operation+1);
            success_irqs(17); assert(step_owned_control(operation)==RISC_STREAM_AGAIN);
            assert(step_owned_control(operation)==17 && native_admission_guard());
            assert(take_owned_control(operation+1,incoming,sizeof(incoming))==RISC_STREAM_INVALID);
            assert(take_owned_control(operation,incoming,sizeof(incoming))==17 && !native_admission_guard());
            if(is("wrapper_tokens")) {
                assert(begin_owned_control(9,0x80,1,0,0,nullptr,17,100,&second)==RISC_STREAM_AGAIN && second==12);
                assert(step_owned_control(operation)==RISC_STREAM_INVALID);
                cancel_owned_control(second); assert(step_owned_control(second)==RISC_STREAM_CANCELLED);
                assert(take_owned_control(second,nullptr,0)==RISC_STREAM_CANCELLED);
                serial=UINT64_MAX; assert(begin_owned_control(9,0x80,1,0,0,nullptr,17,100,&second)==RISC_STREAM_IO && second==0);
            }
        }
    } else { assert(false && "unknown test"); }
    std::printf("control %s PASS\n",name);
}
