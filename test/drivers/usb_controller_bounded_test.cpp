#include "OwnedBulkRequest.h"
#include "usb_bounded_rig.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <array>
using Request = RiscUsbController::OwnedBulkRequest;
using State = Request::State;
struct Port {
    uint64_t clock=0;
    unsigned calls=0, advanceOnCallback=0;
    bool duplicate=false;
    void *resolved=nullptr;
    uint64_t now() const { return clock; }
    esp_err_t library_idle() { ++calls; return risc_usb_host_library_idle(rig_client()); }
    esp_err_t resolve(usb_device_handle_t dev,uint8_t ep,void **out,uint16_t *packet) {
        ++calls; auto rc=risc_usb_host_bulk_resolve(rig_client(),dev,ep,2,0,out,packet);
        if(rc==ESP_OK) resolved=*out;
        return rc;
    }
    esp_err_t submit(void *ep,usb_transfer_t *t) { ++calls; return risc_usb_host_bulk_submit(ep,t); }
    esp_err_t client_step(void *ep, bool *work) {
        ++calls; auto rc=risc_usb_host_client_step(rig_client(),ep,work);
        if(duplicate && *work) rig_transfer()->callback(rig_transfer());
        clock+=advanceOnCallback; return rc;
    }
    esp_err_t cancel_step(void *ep) { ++calls; return risc_usb_host_bulk_cancel_step(ep); }
    esp_err_t clear(void *ep) { ++calls; return risc_usb_host_bulk_clear(ep); }
};
static int32_t step(Request &request,Port &port) {
    auto calls=port.calls;
    const auto result=request.step(port);
    assert(port.calls-calls<=3);
    return result;
}
static void begin(Request &request,Port &port,unsigned budget=100) {
    assert(request.begin(port,rig_transfer(),rig_device(),42,0x81,nullptr,64,budget)==RISC_STREAM_AGAIN);
}
static void active(Request &request,Port &port) {
    begin(request,port);
    step(request,port); assert(request.state()==State::Submit);
    step(request,port); assert(request.state()==State::Active);
    assert(rig_dma_active() && rig_callback_custody());
}
static int32_t finish_cancel(Request &request,Port &port) {
    for(unsigned i=0;i<8 && request.state()!=State::Done && !request.retained();++i) step(request,port);
    assert(!rig_dma_active() && !rig_callback_custody());
    assert(request.state()==State::Done);
    return request.take(42,nullptr,0);
}
int main() {
    unsigned scenarios=0;
    { rig_reset(); rig_legacy_messages(); ++scenarios; }
    // Read copies only on take; take validates claim and destination capacity.
    {
        rig_reset(); Request r; Port p; active(r,p);
        rig_irq(1,13);
        assert(!rig_dma_active() && rig_callback_custody());
        assert(step(r,p)==13 && r.state()==State::Done && r.callback_seen());
        std::array<uint8_t,64> out{};
        assert(r.take(99,out.data(),out.size())==RISC_STREAM_INVALID);
        assert(r.take(42,out.data(),1)==RISC_STREAM_LIMIT && r.owns_storage());
        assert(r.take(42,out.data(),out.size())==13 && !r.owns_storage());
        for(unsigned i=0;i<13;++i) assert(out[i]==0x5a);
        for(unsigned i=13;i<out.size();++i) assert(out[i]==0);
        assert(!rig_transfer()->context && !rig_transfer()->callback);
        ++scenarios;
    }
    // No caller OUT pointer survives begin, including while admission waits.
    {
        rig_reset(); rig_direction(false); Request r; Port p;
        std::array<uint8_t,64> source{}; source.fill(0xa7);
        assert(r.begin(p,rig_transfer(),rig_device(),42,1,source.data(),source.size(),100)==RISC_STREAM_AGAIN);
        source.fill(0);
        for(auto byte:source) assert(byte==0);
        for(unsigned i=0;i<64;++i) assert(rig_transfer()->data_buffer[i]==0xa7);
        step(r,p); step(r,p); rig_irq(1,64);
        assert(step(r,p)==64 && r.take(42,nullptr,0)==64);
        ++scenarios;
    }
    // Host spinlock, USBH mutex and HCD spinlock contend without waiting.
    for(unsigned lock: {0u,2u}) {
        rig_reset(); Request r; Port p; begin(r,p); if(lock==2) step(r,p); rig_lock(lock,true);
        assert(step(r,p)==RISC_STREAM_AGAIN && !rig_dma_active());
        p.clock=100;
        assert(step(r,p)==RISC_STREAM_TIMEOUT && !rig_callback_custody());
        rig_lock(lock,false);
        assert(r.take(42,nullptr,0)==RISC_STREAM_TIMEOUT);
        ++scenarios;
    }
    // Missing library and deferred HUB/USBH work are not an empty inventory.
    {
        rig_reset(); Request r; Port p; begin(r,p); rig_library_missing();
        assert(step(r,p)==RISC_STREAM_IO && !rig_dma_active());
        assert(r.take(42,nullptr,0)==RISC_STREAM_IO); ++scenarios;
    }
    for(unsigned flags=1;flags<=3;++flags) {
        rig_reset(); Request r; Port p; rig_library_pending(flags); begin(r,p);
        for(unsigned i=0;i<16;++i) assert(step(r,p)==RISC_STREAM_AGAIN);
        assert(rig_library_flags()==flags && !rig_dma_active());
        p.clock=100; assert(step(r,p)==RISC_STREAM_TIMEOUT);
        assert(r.take(42,nullptr,0)==RISC_STREAM_TIMEOUT);
        ++scenarios;
    }
    // Client messages are preserved as deferred; no FreeRTOS queue API is
    // invoked from a bounded step, and an empty bulk pipe is not an empty bus.
    {
        rig_reset(); Request r; Port p; active(r,p); rig_queue_events(16,true);
        for(unsigned i=0;i<100;++i) {
            bool work=false;
            assert(risc_usb_host_client_step(rig_client(),p.resolved,&work)==ESP_ERR_NOT_FINISHED && !work);
            assert(rig_events()==0);
        }
        assert(risc_usb_host_library_idle(rig_client())==ESP_ERR_NOT_FINISHED);
        r.cancel(); step(r,p); rig_irq(1,0);
        assert(finish_cancel(r,p)==RISC_STREAM_CANCELLED);
        ++scenarios;
    }
    // Cancellation before any submission is clean and consumes no HAL work.
    {
        rig_reset(); Request r; Port p; begin(r,p); r.cancel();
        assert(step(r,p)==RISC_STREAM_CANCELLED && !rig_halts() && !rig_dma_active());
        assert(r.take(42,nullptr,0)==RISC_STREAM_CANCELLED);
        ++scenarios;
    }
    // Halt begins once; actual ISR acknowledgement + callback + clear permits reuse.
    {
        rig_reset(); Request r; Port p; active(r,p); r.cancel();
        assert(step(r,p)==RISC_STREAM_AGAIN && rig_halt_owned() && rig_halts()==1);
        assert(rig_legacy_command()==ESP_ERR_INVALID_STATE);
        assert(rig_legacy_free()==ESP_ERR_INVALID_STATE);
        for(unsigned i=0;i<10;++i) assert(step(r,p)==RISC_STREAM_AGAIN);
        assert(rig_halts()==1 && rig_callback_custody());
        rig_irq(1,0);
        assert(rig_halt_owned() && !r.callback_seen());
        assert(finish_cancel(r,p)==RISC_STREAM_CANCELLED && !rig_halt_owned());
        // Retirement emptiness was observed under HCD lock; no legacy drain.
        bool work=false; risc_usb_host_client_step(rig_client(),p.resolved,&work);
        assert(!work);
        active(r,p); rig_irq(1,8); assert(step(r,p)==8);
        uint8_t out[8]; assert(r.take(42,out,sizeof(out))==8);
        ++scenarios;
    }
    // Error IRQ wins over requested halt in the real HAL; still acknowledges halt.
    {
        rig_reset(); Request r; Port p; active(r,p); rig_pending_transfer(); r.cancel();
        step(r,p); assert(rig_checked_flush_callbacks()==1);
        assert(finish_cancel(r,p)==RISC_STREAM_CANCELLED); ++scenarios;
    }
    {
        rig_reset(); Request r; Port p; active(r,p); r.cancel(); step(r,p);
        rig_irq(2,0);
        assert(finish_cancel(r,p)==RISC_STREAM_CANCELLED && !rig_halt_owned());
        ++scenarios;
    }
    // The cancellation reserve spends the original absolute deadline only.
    {
        rig_reset(); Request r; Port p; active(r,p);
        p.clock=75; step(r,p); assert(rig_halt_owned() && r.deadline()==100);
        p.clock=76; rig_irq(1,0);
        assert(finish_cancel(r,p)==RISC_STREAM_TIMEOUT);
        ++scenarios;
    }
    // Missing IRQ: deadline retains request, DMA, callback, endpoint and claim.
    // Later real callback returns DMA, but cannot unpin or retry the request.
    {
        rig_reset(); Request r; Port p; active(r,p); p.clock=75; step(r,p);
        p.clock=100; assert(step(r,p)==RISC_STREAM_RETAINED);
        assert(r.owns_storage() && r.claim_id()==42 && rig_dma_active() && rig_halt_owned());
        const auto calls=p.calls, halts=rig_halts();
        r.cancel(); for(unsigned i=0;i<20;++i) assert(step(r,p)==RISC_STREAM_RETAINED);
        assert(p.calls==calls && rig_halts()==halts);
        rig_irq(1,0); bool work=false;
        assert(risc_usb_host_client_step(rig_client(),p.resolved,&work)==ESP_OK && work);
        assert(r.callback_seen() && r.retained() && !rig_dma_active());
        assert(r.take(42,nullptr,0)==RISC_STREAM_RETAINED && r.owns_storage());
        assert(r.begin(p,rig_transfer(),rig_device(),42,0x81,nullptr,64,100)==RISC_STREAM_RETAINED);
        assert(rig_legacy_free()==ESP_ERR_INVALID_STATE);
        ++scenarios;
    }
    // Completion queued before cancellation gets physically retired, never copied to caller.
    {
        rig_reset(); Request r; Port p; active(r,p); rig_irq(1,11); r.cancel();
        step(r,p); assert(finish_cancel(r,p)==RISC_STREAM_CANCELLED);
        ++scenarios;
    }
    // Cancellation after the callback, while pending library work prevents success.
    {
        rig_reset(); Request r; Port p; active(r,p); rig_irq(1,11); rig_library_pending(2);
        step(r,p); assert(r.state()==State::Cancel && rig_library_flags()==2);
        assert(step(r,p)==RISC_STREAM_IO && r.take(42,nullptr,0)==RISC_STREAM_IO);
        ++scenarios;
    }
    // Detach cannot stand in for a HAL halt acknowledgement or callback.
    {
        rig_reset(); Request r; Port p; active(r,p); rig_disconnect(); r.cancel();
        step(r,p); assert(rig_dma_active() && rig_halts()==0 && rig_library_flags()==3);
        p.clock=100; assert(step(r,p)==RISC_STREAM_RETAINED);
        assert(r.owns_storage() && rig_callback_custody());
        ++scenarios;
    }
    // Disconnect after a real IRQ makes endpoint clear fail, so custody stays pinned.
    {
        rig_reset(); Request r; Port p; active(r,p); r.cancel(); step(r,p);
        rig_irq(1,0); step(r,p); step(r,p); assert(r.state()==State::Clear);
        rig_disconnect(); assert(step(r,p)==RISC_STREAM_RETAINED && r.owns_storage());
        ++scenarios;
    }
    // Lock contention after submission cannot be converted into a clean timeout.
    for(unsigned lock: {0u,2u}) {
        rig_reset(); Request r; Port p; active(r,p); rig_lock(lock,true);
        p.clock=75; step(r,p); p.clock=100;
        assert(step(r,p)==RISC_STREAM_RETAINED && rig_dma_active());
        rig_lock(lock,false); ++scenarios;
    }
    // Impossible completion length rejects copy-out but has real returned DMA.
    {
        rig_reset(); Request r; Port p; active(r,p); rig_irq(1,64);
        rig_transfer()->actual_num_bytes=65;
        assert(step(r,p)==RISC_STREAM_IO && r.take(42,nullptr,0)==RISC_STREAM_IO);
        ++scenarios;
    }
    // Clock reversal and observed overrun cannot thaw an accepted request.
    {
        rig_reset(); Request r; Port p; p.clock=10; active(r,p); p.clock=9;
        assert(step(r,p)==RISC_STREAM_RETAINED && rig_callback_custody()); ++scenarios;
    }
    {
        rig_reset(); Request r; Port p; active(r,p); p.advanceOnCallback=101;
        assert(step(r,p)==RISC_STREAM_RETAINED && rig_callback_custody()); ++scenarios;
    }
    // Claim scope is checked against the real endpoint's interface, not merely client.
    {
        rig_reset(); Request r; Port p; rig_interface(3,0); begin(r,p);
        assert(step(r,p)==RISC_STREAM_IO && !rig_dma_active()); ++scenarios;
    }
    // More than one URB is rejected before the stock flush loop is entered.
    {
        rig_reset(); Request r; Port p; active(r,p); rig_extra_pending(2); r.cancel();
        step(r,p); assert(rig_halts()==0);
        p.clock=100; assert(step(r,p)==RISC_STREAM_RETAINED); ++scenarios;
    }
    // Invalid length/budget, transfer capacity and MPS fail before physical submission.
    {
        rig_reset(); Request r; Port p;
        assert(r.begin(p,rig_transfer(),rig_device(),42,0x81,nullptr,4097,100)==RISC_STREAM_INVALID);
        assert(r.begin(p,rig_transfer(),rig_device(),42,0x81,nullptr,64,1001)==RISC_STREAM_INVALID);
        rig_packet(0); begin(r,p); assert(step(r,p)==RISC_STREAM_IO && !rig_dma_active()); ++scenarios;
    }
    // Duplicate completion is a custody fault even after the first callback.
    {
        rig_reset(); Request r; Port p; active(r,p); rig_irq(1,0); step(r,p);
        auto *t=rig_transfer(); t->callback(t);
        assert(r.retained() && step(r,p)==RISC_STREAM_RETAINED);
        assert(r.take(42,nullptr,0)==RISC_STREAM_RETAINED); ++scenarios;
    }
    // A duplicate callback while Drain is running cannot overwrite Retained.
    {
        rig_reset(); Request r; Port p; active(r,p); r.cancel(); step(r,p);
        rig_irq(1,0); step(r,p); assert(r.state()==State::Drain);
        p.duplicate=true;
        assert(step(r,p)==RISC_STREAM_RETAINED && r.retained());
        assert(r.take(42,nullptr,0)==RISC_STREAM_RETAINED); ++scenarios;
    }
    // Absolute deadline overflow rejects the request without taking the buffer.
    {
        rig_reset(); Request r; Port p; p.clock=UINT64_MAX-20;
        assert(r.begin(p,rig_transfer(),rig_device(),42,0x81,nullptr,64,100)==RISC_STREAM_IO);
        assert(!r.owns_storage() && !rig_transfer()->context); ++scenarios;
    }
    std::printf("Owned bulk + exact staged IDF event/ISR/retirement: %u fault scenarios PASS\n",scenarios);
}
