#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../Apps/hollow_trail.c"
static uint32_t now,mapped;
static bool healthy=true,host_exit;
static risc_usb_gamepad_state_v1 reports[2][2];
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    now+=wait; memset(out,0,sizeof(*out)); out->buttons=mapped; out->exit_requested=host_exit; return true;
}
static uint32_t millis_now(void) {return now;}
static const t5_app_api_v1 fake_app={.abi_version=1,.struct_size=sizeof(fake_app),.poll=fake_poll,.millis=millis_now};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &fake_app;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
static bool raw_poll(void *ctx,size_t n){(void)ctx;(void)n;return healthy;}
static bool snapshot(void *ctx,risc_usb_gamepad_state_v1 *out,size_t *count) {
    assert(*count>=2); memcpy(out,reports[(uintptr_t)ctx],sizeof(reports[0])); *count=2; return healthy;
}
static const risc_usb_gamepad_api_v1 xapi={.api_version=1,.struct_size=sizeof(xapi),.context=(void*)0,.poll=raw_poll,.snapshot=snapshot};
static const risc_usb_gamepad_api_v1 hapi={.api_version=1,.struct_size=sizeof(hapi),.context=(void*)1,.poll=raw_poll,.snapshot=snapshot};
static void press(unsigned source,uint32_t mask) {
    reports[source][0].buttons=0; ht_input(1);
    reports[source][0].buttons=mask; ht_input(1);
}
int main(void) {
    app=&fake_app; pad=&xapi; hid_pad=&hapi;
    /* Masks copied from GameBoy's physical-button mapping, not derived from app code. */
    const unsigned a[2]={2,1},b[2]={1,2},x[2]={8,4},y[2]={4,8},start[2]={512,128},select[2]={256,64};
    for(unsigned source=0;source<2;++source) {
        memset(reports,0,sizeof(reports)); reports[source][0].connected=1;
        reports[source][0].device=source+1; reports[source][0].hat=8;
        memset(&ht,0,sizeof(ht)); ht_spawn(true); ht_geometry_level=0;
        reading=paused=quitting=loading=jump_down=pause_down=false;
        previous=held=0; ht_pad_owned=ht_input_rearm=false; ht_pad_source=-1; simulation_started=false;
        mapped=T5_APP_BUTTON_UP|T5_APP_BUTTON_DOWN|T5_APP_BUTTON_CONFIRM;
        ht_input(1); // Neutral raw state suppresses duplicate mapped actions.
        assert(!reading && !paused && !jump_down && !quitting);
        for(unsigned hat=0;hat<8;++hat) { reports[source][0].hat=(uint8_t)hat; ht_input(1); assert(!reading && !jump_down); }
        reports[source][0].hat=8;
        press(source,b[source]); assert(jump_down && !reading && !(held&HT_INTERACT));
        jump_down=false;
        reports[source][0].buttons=0; ht_input(1);
        mapped|=T5_APP_BUTTON_BACK; // Even duplicate mapped Back must not turn A into Exit.
        reports[source][0].buttons=a[source]; ht_input(1); assert(!reading && !jump_down && !quitting);
        mapped&=~T5_APP_BUTTON_BACK; // Inspect empty space is inert.
        ht.x=ht_evidence_x(0,0)*256; ht.y=ht_land[1].top*256; ht.vy=0; ht.grounded=true;
        press(source,a[source]); assert(reading && journal_page==0 && ht_evidence_found(&ht,0));
        press(source,x[source]); assert(ht_journal_index && !quitting);
        press(source,x[source]); assert(!reading && !quitting);
        ht_input(1); assert(!quitting); // Held Back does not also exit.
        press(source,start[source]); assert(reading && ht_journal_index);
        ht_input(1); assert(reading);
        press(source,start[source]); assert(!reading);
        press(source,y[source]); assert(!reading && !paused && !jump_down);
        if(!source) { press(source,64|128); assert(!reading && !paused); } // Triggers are not Start/Select.
        press(source,select[source]); reports[source][0].buttons=0; ht_input(1); assert(paused && !reading);
        paused=false; pause_down=false;
        healthy=false; mapped=T5_APP_BUTTON_CONFIRM|T5_APP_BUTTON_UP|T5_APP_BUTTON_DOWN;
        ht_input(1); assert(!reading && !jump_down && !pause_down);
        healthy=true; reports[source][0].buttons=start[source]; ht_input(1); assert(!reading);
        press(source,start[source]); assert(reading); // Recovery requires a release first.
        press(source,start[source]); assert(!reading);
        reports[source][1]=reports[source][0]; reports[source][1].buttons=start[source];
        reports[source][0].buttons=0; ht_input(1); assert(!reading); // Never OR a second receiver slot.
        reports[source][1].connected=0;
        press(source,x[source]); assert(!quitting); // X only backs out of reading.
        reports[source][0].buttons=0; mapped=0; ht_input(1);
        mapped=T5_APP_BUTTON_BACK; ht_input(1); assert(quitting);
        mapped=0; quitting=false; host_exit=true; ht_input(1); assert(quitting);
        host_exit=false;
    }
    /* Generic arrow keys cannot open the journal or inspect. Down pauses;
     * Confirm from pause is the explicit no-controller route to the journal. */
    memset(reports,0,sizeof(reports)); mapped=0; quitting=reading=paused=false;
    jump_down=pause_down=false; ht_input(1);
    mapped=T5_APP_BUTTON_UP; ht_input(1); assert(jump_down && !reading);
    jump_down=false; mapped=0; ht_input(1);
    mapped=T5_APP_BUTTON_DOWN; ht_input(1); mapped=0; ht_input(1); assert(paused && !reading);
    mapped=T5_APP_BUTTON_CONFIRM; ht_input(1); assert(reading);
    /* A persistently failed provider hands back device input after a bounded
     * grace period, still requiring neutral before any mapped action. */
    reading=paused=quitting=false; mapped=0; ht_input(1);
    reports[0][0].connected=1; reports[0][0].device=99; reports[0][0].hat=8;
    ht_input(1); healthy=false; ht_input(1); now+=251; ht_input(1);
    mapped=0; ht_input(1); mapped=T5_APP_BUTTON_BACK; ht_input(1); assert(quitting);
    puts("Hollow Trail controls: GameBoy HID/XInput masks, dedicated Start, A inspect, B jump, X back, arbitration and fault recovery PASS");
}
