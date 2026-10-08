#include "OwnedAdmissionRequest.h"
#include "OwnedBulkRequest.h"
#include "usb_admission_rig.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Request=RiscUsbController::OwnedAdmissionRequest;
using Kind=Request::Kind;
using State=Request::State;
struct Clock { uint64_t ticks=10; uint64_t now() const { return ticks; } };
namespace Wrappers {
static Request ownedAdmission;
static uint64_t admissionDevice;
static struct { uint64_t token; bool interfaceReleased; } claims[2];
static struct { uint64_t token; bool referenceClosed; } devices[2];
#include "admission_wrapper.inc"
}
struct BulkPort {
    Clock &clock;
    uint64_t now() const { return clock.now(); }
    esp_err_t library_idle() { return risc_usb_host_library_idle(rig_client()); }
    esp_err_t resolve(usb_device_handle_t d,uint8_t ep,void **out,uint16_t *mps) {
        return risc_usb_host_bulk_resolve(rig_client(),d,ep,0,0,out,mps);
    }
    esp_err_t submit(void *ep,usb_transfer_t *t) { return risc_usb_host_bulk_submit(ep,t); }
    esp_err_t client_step(void *ep,bool *work) { return risc_usb_host_client_step(rig_client(),ep,work); }
    esp_err_t cancel_step(void *ep) { return risc_usb_host_bulk_cancel_step(ep); }
    esp_err_t clear(void *ep) { return risc_usb_host_bulk_clear(ep); }
};
static int32_t begin(Request &r,Clock &c,Kind kind,uint64_t token=100,unsigned number=0,unsigned budget=1000,bool close=false) {
    return r.begin(c,kind,rig_client(),rig_device(),token,number,0,close,budget);
}
static int32_t run(Request &r,Clock &c) {
    for(unsigned i=0;i<1200;++i) {
        int32_t rc=r.step(c);
        if(r.state()==State::Done || r.retained()) return rc;
        ++c.ticks;
    }
    std::abort();
}
static void claim(Clock &c,uint64_t token=100,unsigned number=0) {
    Request r; assert(begin(r,c,Kind::Claim,token,number)==RISC_STREAM_AGAIN);
    assert(run(r,c)==RISC_STREAM_OK); assert(r.take(token)==RISC_STREAM_OK);
}
static void release(Clock &c,uint64_t token=100) {
    Request r; assert(begin(r,c,Kind::Release,token)==RISC_STREAM_AGAIN);
    assert(run(r,c)==RISC_STREAM_OK); assert(r.take(token)==RISC_STREAM_OK);
}
static risc_usb_claim_state state(uint64_t token=100) {
    risc_usb_claim_state s{}; assert(risc_usb_claim_state_copy(token,&s)); return s;
}
static void reach_partial(Request &r,Clock &c) {
    assert(begin(r,c,Kind::Claim)==RISC_STREAM_AGAIN);
    for(unsigned i=0;i<100;++i) {
        r.step(c); ++c.ticks;
        risc_usb_claim_state s{};
        if(risc_usb_claim_state_copy(100,&s) && s.acquired==1) return;
    }
    std::abort();
}
int main(int argc,char **argv) {
    assert(argc==2); const char *test=argv[1]; admission_init(); Clock clock;
    if(!std::strcmp(test,"prepare")) {
        for(unsigned fail=1;fail<=6;++fail) {
            assert(admission_prepare(fail)==ESP_ERR_NO_MEM);
            assert(!admission_live_allocations() && !risc_usb_admission_owns_resources());
        }
        assert(admission_prepare(0)==ESP_OK && admission_live_allocations()==6);
        assert(admission_dispose()==ESP_OK && !admission_live_allocations());
    } else if(!std::strcmp(test,"unprepared")) {
        Request r; begin(r,clock,Kind::Claim);
        assert(run(r,clock)==RISC_STREAM_IO && !admission_channels());
        assert(r.take(100)==RISC_STREAM_IO && !admission_allocations());
    } else if(!std::strcmp(test,"config")) {
        Request r; begin(r,clock,Kind::Configuration,200);
        assert(run(r,clock)==RISC_STREAM_OK && !admission_allocations());
        admission_config_corrupt(0,0); uint8_t out[4096]{}; size_t size=1; uint16_t vid=0,pid=0;
        assert(r.take(201,out,&size,&vid,&pid)==RISC_STREAM_INVALID);
        assert(r.take(200,out,&size,&vid,&pid)==RISC_STREAM_LIMIT && size==55 && out[0]==0);
        size=sizeof(out); assert(r.take(200,out,&size,&vid,&pid)==RISC_STREAM_OK);
        assert(out[0]==9 && size==55 && vid==0x1234 && pid==0x5678);
    } else {
        assert(admission_prepare(0)==ESP_OK);
        const unsigned allocated=admission_allocations();
        if(!std::strcmp(test,"success")) {
            claim(clock); auto s=state(); assert(s.token==100 && s.published && s.acquired==2 && admission_channels()==2);
            release(clock); assert(!risc_usb_claim_state_copy(100,&s) && !admission_channels());
            claim(clock,101); release(clock,101);
            assert(admission_dispose()==ESP_OK && !admission_live_allocations());
        } else if(!std::strcmp(test,"bulk_claim")) {
            claim(clock); BulkPort port{clock}; RiscUsbController::OwnedBulkRequest bulk;
            uint8_t bytes[64]{};
            assert(bulk.begin(port,rig_transfer(),rig_device(),100,1,bytes,sizeof(bytes),100)==RISC_STREAM_AGAIN);
            bulk.step(port); bulk.step(port);
            assert(bulk.state()==RiscUsbController::OwnedBulkRequest::State::Active);
            admission_bulk_irq(100,0,1,64);
            assert(bulk.step(port)==64 && bulk.take(100,nullptr,0)==64);
            release(clock); assert(!admission_channels() && admission_dispose()==ESP_OK);
        } else if(!std::strcmp(test,"take_stale")) {
            admission_config_corrupt(9,1);
            Wrappers::claims[0].token=100; Wrappers::claims[1].token=200;
            Wrappers::admissionDevice=1;
            begin(Wrappers::ownedAdmission,clock,Kind::Claim);
            assert(run(Wrappers::ownedAdmission,clock)==RISC_STREAM_IO);
            assert(Wrappers::take_owned_admission(100,nullptr,nullptr,nullptr,nullptr)==RISC_STREAM_IO);
            assert(!Wrappers::claims[0].token && Wrappers::claims[1].token==200);
            assert(Wrappers::take_owned_admission(200,nullptr,nullptr,nullptr,nullptr)==RISC_STREAM_INVALID);
            assert(Wrappers::claims[1].token==200);
        } else if(!std::strcmp(test,"claim_commit_lock")) {
            Request r; reach_partial(r,clock);
            while(state().acquired!=2) { r.step(clock); ++clock.ticks; }
            assert(!state().published); rig_lock(0,true); clock.ticks=2000;
            assert(r.step(clock)==RISC_STREAM_RETAINED && state().token==100 && state().acquired==2);
            rig_lock(0,false);
            assert(risc_usb_claim_step(100)==ESP_ERR_INVALID_STATE && !state().published);
        } else if(!std::strcmp(test,"release_commit_lock")) {
            claim(clock); Request r; begin(r,clock,Kind::Release); r.step(clock);
            while(state().acquired) { r.step(clock); ++clock.ticks; }
            assert(state().published && !admission_channels());
            rig_lock(0,true); clock.ticks=2000;
            assert(r.step(clock)==RISC_STREAM_RETAINED && state().retained);
            rig_lock(0,false); assert(admission_dispose()==ESP_ERR_INVALID_STATE);
        } else if(!std::strcmp(test,"release_close")) {
            claim(clock); admission_device_state(1);
            Request r; begin(r,clock,Kind::Release,100,0,1000,true);
            assert(run(r,clock)==RISC_STREAM_RETAINED && r.token()==100);
            assert(r.interface_released() && r.reference_closed() && r.cleanup_deferred());
            assert(!admission_channels() && !admission_device_refs() && rig_library_flags()==1);
            assert(r.step(clock)==RISC_STREAM_RETAINED);
        } else if(!std::strcmp(test,"owned_descriptors")) {
            claim(clock); admission_config_corrupt(20,0xff); admission_config_corrupt(13,0);
            assert(state().endpoints==2); release(clock);
            assert(!admission_channels() && admission_dispose()==ESP_OK);
        } else if(!std::strcmp(test,"endpoint_reject")) {
            admission_config_corrupt(28,3); /* Second endpoint is interrupt. */
            admission_config_corrupt(31,0); /* Its interval is invalid. */
            Request r; begin(r,clock,Kind::Claim);
            assert(run(r,clock)==RISC_STREAM_UNSUPPORTED && !r.retained());
            assert(!admission_channels() && !risc_usb_admission_owns_resources());
        } else if(!std::strcmp(test,"zero")) {
            admission_config(16,0);
            for(unsigned i=0;i<16;++i) claim(clock,100+i,i);
            Request r; begin(r,clock,Kind::Claim,116,16);
            assert(run(r,clock)==RISC_STREAM_LIMIT && !admission_channels());
            for(unsigned i=0;i<16;++i) release(clock,100+i);
            assert(admission_dispose()==ESP_OK);
        } else if(!std::strcmp(test,"exhaust")) {
            admission_config(4,2);
            for(unsigned i=0;i<3;++i) claim(clock,100+i,i);
            assert(admission_channels()==6);
            Request r; begin(r,clock,Kind::Claim,103,3);
            assert(run(r,clock)==RISC_STREAM_LIMIT && admission_channels()==6);
            risc_usb_claim_state s{}; assert(!risc_usb_claim_state_copy(103,&s));
            for(unsigned i=0;i<3;++i) { assert(state(100+i).published); release(clock,100+i); }
            assert(admission_dispose()==ESP_OK);
        } else if(!std::strcmp(test,"partial")) {
            admission_channel_occupancy(0x7f); Request r; reach_partial(r,clock);
            admission_hcd_fault(100,0,3,true);
            assert(run(r,clock)==RISC_STREAM_RETAINED);
            auto s=state(); assert(s.token==100 && s.retained && s.acquired==1 && !s.published);
            assert(admission_channels()==1 && admission_dispose()==ESP_ERR_INVALID_STATE);
            admission_hcd_fault(100,0,3,false);
            for(unsigned i=0;i<10;++i) assert(r.step(clock)==RISC_STREAM_RETAINED);
            assert(risc_usb_claim_release_step(100)==ESP_ERR_INVALID_STATE && admission_channels()==1);
        } else if(!std::strcmp(test,"cancel_clean")) {
            Request r; begin(r,clock,Kind::Claim); r.cancel();
            assert(run(r,clock)==RISC_STREAM_CANCELLED && !risc_usb_admission_owns_resources());
        } else if(!std::strcmp(test,"cancel_partial")) {
            Request r; reach_partial(r,clock); r.cancel();
            assert(run(r,clock)==RISC_STREAM_CANCELLED && !admission_channels());
            risc_usb_claim_state s{}; assert(!risc_usb_claim_state_copy(100,&s));
        } else if(!std::strcmp(test,"deadline_clean")) {
            Request r; begin(r,clock,Kind::Claim); r.step(clock); clock.ticks=2000;
            assert(r.step(clock)==RISC_STREAM_TIMEOUT && !risc_usb_admission_owns_resources());
        } else if(!std::strcmp(test,"deadline_partial")) {
            Request r; reach_partial(r,clock); clock.ticks=2000;
            assert(r.step(clock)==RISC_STREAM_RETAINED && state().acquired==1);
            assert(r.take(100)==RISC_STREAM_RETAINED && admission_channels()==1);
        } else if(!std::strncmp(test,"close",5)) {
            if(!std::strcmp(test,"close_gone")) admission_device_state(1);
            else if(!std::strcmp(test,"close_waiting")) admission_device_state(2);
            else if(!std::strcmp(test,"close_pending")) admission_device_state(3);
            else if(!std::strcmp(test,"close_ctrl")) admission_device_state(4);
            else if(!std::strcmp(test,"close_claimed")) claim(clock);
            Request r; begin(r,clock,Kind::Close,200);
            int32_t answer=run(r,clock);
            if(!std::strcmp(test,"close")) {
                assert(answer==RISC_STREAM_OK && r.reference_closed() && !r.cleanup_deferred());
                assert(!admission_device_refs() && !admission_device_opened());
            } else if(!std::strcmp(test,"close_gone") || !std::strcmp(test,"close_waiting")) {
                assert(answer==RISC_STREAM_RETAINED && r.reference_closed() && r.cleanup_deferred());
                assert(!admission_device_refs() && !admission_device_opened() && rig_library_flags()==1);
                assert(admission_device_actions()==(!std::strcmp(test,"close_gone")?0x40u:0x80u));
                for(unsigned i=0;i<5;++i) assert(r.step(clock)==RISC_STREAM_RETAINED);
            } else if(!std::strcmp(test,"close_pending")) {
                assert(answer==RISC_STREAM_RETAINED && !r.reference_closed());
                assert(admission_device_refs()==1 && admission_device_opened() && admission_device_actions()==0x10);
            } else {
                assert(answer==RISC_STREAM_IO && admission_device_refs()==1 && admission_device_opened());
            }
            bool closed=true,deferred=true;
            auto rc=risc_usb_device_close_try(rig_client(),rig_device(),&closed,&deferred);
            assert(rc!=ESP_OK && !closed); /* Never decrement a closed reference twice. */
        } else if(!std::strcmp(test,"stale_callback")) {
            claim(clock); admission_endpoint_callback(100,0,true);
            assert(risc_usb_admission_faulted() && state().retained && admission_channels()==2);
            Request r; begin(r,clock,Kind::Release); assert(run(r,clock)==RISC_STREAM_RETAINED);
            assert(admission_dispose()==ESP_ERR_INVALID_STATE);
        } else if(!std::strcmp(test,"late_callback")) {
            claim(clock); uint32_t saved=admission_endpoint_cookie(100,0); release(clock);
            claim(clock,101); assert(admission_endpoint_cookie(101,0)!=saved);
            admission_saved_callback(saved);
            assert(risc_usb_admission_faulted() && state(101).retained && admission_channels()==2);
        } else if(!std::strcmp(test,"scope")) {
            claim(clock); Request duplicate; begin(duplicate,clock,Kind::Claim,101);
            assert(run(duplicate,clock)==RISC_STREAM_IO && admission_channels()==2);
            Request wrong; begin(wrong,clock,Kind::Release,999);
            assert(run(wrong,clock)==RISC_STREAM_CLOSED && state().token==100);
            release(clock); claim(clock,102); release(clock,102);
        } else if(!std::strcmp(test,"malformed")) {
            for(unsigned i=0;i<3;++i) {
                admission_config(2,2);
                if(i==0) admission_config_corrupt(9,1);
                if(i==1) admission_config_corrupt(27,1);
                if(i==2) admission_config_corrupt(13,3);
                Request r; begin(r,clock,Kind::Claim,100+i);
                assert(run(r,clock)<0 && !r.retained() && !admission_channels() && !risc_usb_admission_owns_resources());
            }
        } else if(!std::strcmp(test,"retained_release")) {
            claim(clock); Request r; begin(r,clock,Kind::Release); r.step(clock); r.step(clock);
            assert(state().acquired==1 && admission_channels()==1);
            admission_hcd_fault(100,0,3,true);
            assert(run(r,clock)==RISC_STREAM_RETAINED && state().retained);
            admission_hcd_fault(100,0,3,false);
            assert(r.step(clock)==RISC_STREAM_RETAINED && admission_channels()==1);
        } else if(!std::strcmp(test,"clock")) {
            Request r; reach_partial(r,clock); clock.ticks=1;
            assert(r.step(clock)==RISC_STREAM_RETAINED && state().acquired==1);
        } else if(!std::strcmp(test,"legacy_release")) {
            claim(clock); assert(admission_legacy_release(100)==ESP_ERR_INVALID_STATE && admission_channels()==2);
            assert(admission_legacy_pipe_free(100,0)==ESP_ERR_INVALID_STATE && admission_channels()==2);
            release(clock);
        } else if(!std::strcmp(test,"deferred_claim")) {
            rig_library_pending(3); Request r; begin(r,clock,Kind::Claim);
            assert(run(r,clock)==RISC_STREAM_TIMEOUT && rig_library_flags()==3 && !admission_channels());
            assert(!risc_usb_admission_owns_resources());
        } else if(!std::strncmp(test,"lock_",5)) {
            unsigned which=std::strtoul(test+5,nullptr,10); rig_lock(which,true);
            Request r; begin(r,clock,Kind::Claim);
            assert(run(r,clock)==RISC_STREAM_TIMEOUT && !admission_channels());
            rig_lock(which,false); assert(!risc_usb_admission_owns_resources());
        } else if(!std::strncmp(test,"hcd_",4) || !std::strncmp(test,"host_",5)) {
            bool hcd=test[1]=='c'; unsigned which=std::strtoul(test+(hcd?4:5),nullptr,10);
            claim(clock);
            if(hcd) admission_hcd_fault(100,1,which,true);
            else admission_host_fault(100,1,which,true);
            Request r; begin(r,clock,Kind::Release);
            assert(run(r,clock)==RISC_STREAM_RETAINED && state().retained && state().acquired==2);
            assert(admission_channels()==2 && admission_dispose()==ESP_ERR_INVALID_STATE);
        } else std::abort();
        assert(admission_allocations()==allocated); /* No hidden allocation in any bounded operation. */
    }
    std::printf("Owned native admission %s: PASS\n",test);
}
