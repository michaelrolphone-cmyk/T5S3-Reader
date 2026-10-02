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
static void schoolroom_controls(void) {
    const unsigned a[2]={1,2},x[2]={8,4},y[2]={4,8},start[2]={512,128};
    healthy=true;host_exit=false;app=&fake_app;pad=&xapi;hid_pad=&hapi;
    for(unsigned source=0;source<2;++source) {
        memset(reports,0,sizeof(reports));reports[source][0].connected=1;
        reports[source][0].device=source+1;reports[source][0].hat=8;
        memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
        ht.x=HT_SCHOOL_MAP_X*256;ht.y=ht_surface_at(&ht,1,HT_SCHOOL_MAP_X)*256;
        ht.grounded=true;ht_cutscene.active=false;ht_cutscene_seen=0;
        reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
        ht_schoolroom_studying=false;held=previous=mapped=0;
        ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
        ht_input(1);press(source,a[source]);
        assert(ht_schoolroom_studying && !reading && ht_schoolroom_focus==0);
        assert(ht_evidence_found(&ht,3));ht_game frozen=ht;
        for(unsigned i=0;i<20;++i){ht_advance(now+=HT_STEP_MS);assert(!memcmp(&ht,&frozen,sizeof(ht)));}
        press(source,y[source]);ht_advance(now+=HT_STEP_MS);
        assert(ht_schoolroom_studying && !ht_motion_emphasis && !memcmp(&ht,&frozen,sizeof(ht)));
        reports[source][0].buttons=0;ht_input(1);
        reports[source][0].hat=2;ht_input(1);assert(ht_schoolroom_focus==1);
        ht_input(1);assert(ht_schoolroom_focus==1); /* Held direction never repeats. */
        reports[source][0].hat=8;ht_input(1);reports[source][0].hat=2;ht_input(1);
        assert(ht_schoolroom_focus==2);
        reports[source][0].hat=8;ht_input(1);reports[source][0].hat=2;ht_input(1);
        assert(ht_schoolroom_focus==0);reports[source][0].hat=8;ht_input(1);
        reports[source][0].hat=6;ht_input(1);assert(ht_schoolroom_focus==2);
        reports[source][0].hat=8;ht_input(1);
        press(source,x[source]);assert(!ht_schoolroom_studying && !reading && !quitting && ht_input_rearm);
        assert(!memcmp(&ht,&frozen,sizeof(ht)));
        reports[source][0].hat=2;ht_input(1);assert(!held && ht_input_rearm);
        reports[source][0].hat=8;reports[source][0].buttons=0;ht_input(1);assert(!ht_input_rearm);
        press(source,a[source]);assert(ht_schoolroom_studying);
        ht_input(1);assert(ht_schoolroom_studying && !reading); /* Entry A is not read-note A. */
        press(source,a[source]);assert(reading && journal_page==3 && !ht_schoolroom_studying);
        press(source,x[source]);assert(ht_journal_index && reading);
        press(source,x[source]);assert(!reading && !quitting);
        press(source,a[source]);assert(ht_schoolroom_studying);
        press(source,start[source]);assert(reading && !ht_schoolroom_studying && journal_page==3);
        reading=false;reports[source][0].buttons=0;ht_input(1);
        press(source,a[source]);assert(ht_schoolroom_studying);
        host_exit=true;ht_input(1);assert(quitting);host_exit=false;
        ht_schoolroom_studying=false;
    }
}
/* Drive the real provider mapping and scheduler, not the emphasis helper. */
static void motion_emphasis_controls(void) {
    const unsigned x[2]={8,4},y[2]={4,8};
    healthy=true;host_exit=false;app=&fake_app;pad=&xapi;hid_pad=&hapi;
    for(unsigned source=0;source<2;++source) {
        int velocity[3];
        for(unsigned button=0;button<3;++button) {
            memset(reports,0,sizeof(reports));
            reports[source][0].connected=1;reports[source][0].device=source+1;
            reports[source][0].hat=8;
            memset(&ht,0,sizeof(ht));ht_spawn(true);ht_geometry_level=0;
            ht_cutscene.active=false;ht_cutscene_seen=0;
            reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
            held=previous=mapped=0;ht_pad_owned=ht_input_rearm=false;
            ht_pad_source=-1;simulation_started=false;
            ht_input(1); /* Neutral owns/rearms this receiver. */
            reports[source][0].hat=2;
            reports[source][0].buttons=button==1?x[source]:button==2?y[source]:0;
            ht_input(1);ht_advance(now);ht_advance(now+=HT_STEP_MS);
            velocity[button]=ht.vx;
            assert(ht_motion_emphasis==(button==2));
            assert(!reading && !paused && !jump_down && !quitting);
            assert(((held&HT_BACK)!=0)==(button==1));
            assert(((held&HT_EMPHASIS)!=0)==(button==2));
            if(button==2) {
                reports[source][0].buttons=0;ht_input(1);ht_advance(now+=HT_STEP_MS);
                assert(!ht_motion_emphasis && !(held&HT_EMPHASIS));
                reports[source][0].buttons=y[source];ht_input(1);ht_advance(now+=HT_STEP_MS);
                assert(ht_motion_emphasis);
                paused=true;ht_advance(now+=HT_STEP_MS);assert(!ht_motion_emphasis);
                paused=false;reading=true;ht_advance(now+=HT_STEP_MS);assert(!ht_motion_emphasis);
                reading=false;ht_cutscene_begin(HT_CUTSCENE_INTRO);
                ht_advance(now+=HT_STEP_MS);assert(!ht_motion_emphasis);
            }
        }
        assert(velocity[0]==48 && velocity[1]==velocity[0] && velocity[2]==53);
    }
}
static void rain_controls(void) {
    const unsigned a[2]={1,2},b[2]={2,1},y[2]={4,8};
    app=&fake_app;pad=&xapi;hid_pad=&hapi;
    for(unsigned source=0;source<2;++source) {
        memset(reports,0,sizeof(reports));reports[source][0].connected=1;
        reports[source][0].device=source+1;reports[source][0].hat=8;
        healthy=true;host_exit=false;mapped=0;
        memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
        ht.x=(HT_RAIN_TANK_X-1)*256;ht.y=ht_rain_tank_floor()*256;ht.grounded=true;
        ht_cutscene.active=false;ht_cutscene_seen=0;
        reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
        ht_schoolroom_studying=false;held=previous=0;
        ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
        ht_input(1);reports[source][0].hat=2;
        for(unsigned i=0;i<12 && !ht_cutscene.active;++i)ht_input(32);
        assert(ht_cutscene.active && ht_cutscene.id==HT_CUTSCENE_RAIN);
        ht_game frozen=ht;
        reports[source][0].buttons=a[source]|b[source]|y[source];
        for(unsigned i=0;i<30;++i){ht_input(32);assert(!memcmp(&ht,&frozen,sizeof(ht)));}
        assert(!ht_motion_emphasis && !reading && !paused && !jump_down);
        /* Provider failure may rearm ownership, but cannot mutate the scene. */
        healthy=false;for(unsigned i=0;i<9;++i)ht_input(32);
        assert(!memcmp(&ht,&frozen,sizeof(ht)));healthy=true;
        while(ht_cutscene.active)ht_input(32);
        assert(ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
        ht_input(32);assert(!held && !jump_down && ht_input_rearm);
        reports[source][0].hat=8;reports[source][0].buttons=0;ht_input(1);
        assert(!ht_input_rearm);reports[source][0].hat=2;ht_input(32);
        assert(held&HT_RIGHT);
        ht_game before=ht;before.x=(HT_RAIN_TANK_X-1)*256;
        assert(!ht_cutscene_rain_arrival(&before,&ht)); /* Once per session. */
        ht_cutscene_begin(HT_CUTSCENE_RAIN);host_exit=true;ht_input(1);
        assert(quitting);host_exit=false;ht_cutscene.active=false;
    }
}
int main(void) {
    app=&fake_app; pad=&xapi; hid_pad=&hapi;
    /* Receiver face-label correction reported on hardware for 1.0.15. */
    const unsigned a[2]={1,2},b[2]={2,1},x[2]={8,4},y[2]={4,8},start[2]={512,128},select[2]={256,64};
    for(unsigned source=0;source<2;++source) {
        memset(reports,0,sizeof(reports)); reports[source][0].connected=1;
        reports[source][0].device=source+1; reports[source][0].hat=8;
        memset(&ht,0,sizeof(ht)); ht_spawn(true); ht_geometry_level=0;
        reading=paused=quitting=loading=jump_down=pause_down=false;
        ht_camera_mode=HT_CAMERA_BASELINE;

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
        ht.x=ht_landmark_x(0,0)*256;ht.y=ht_surface_at(&ht,3,ht.x/256)*256;ht.grounded=true;
        press(source,a[source]);assert(ht.observation==1 && ht.observation_ticks==220 && !reading && !quitting);
        ht_spawn(true);
        ht.x=ht_evidence_x(0,0)*256; ht.y=ht_surface_at(&ht,0,ht.x/256)*256; ht.vy=0; ht.grounded=true;
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
        for(unsigned mode=1;mode<=HT_CAMERA_MODES;++mode) {
            press(source,b[source]);
            assert(paused && !jump_down && ht_camera_mode==mode%HT_CAMERA_MODES);
            ht_input(1);assert(ht_camera_mode==mode%HT_CAMERA_MODES);
        }
        paused=false; pause_down=false;
        /* Pause level picker: all ten destinations, clean respawn, preserved
         * discoveries, and no held-A action leaking into the new chapter. */
        for(unsigned destination=0;destination<HT_LEVELS;++destination) {
            ht_select_level(ht.level);reading=paused=false;pause_down=false;
            press(source,select[source]);reports[source][0].buttons=0;ht_input(1);
            assert(paused);
            unsigned target=(ht.level+1)%HT_LEVELS;
            reports[source][0].hat=2;ht_input(1);
            assert(debug_select && debug_level==target && ht.level!=target);
            ht_input(1);assert(debug_level==target); // Held direction advances once.
            reports[source][0].hat=8;ht_input(1);
            uint32_t found=ht.evidence;
            press(source,a[source]);
            assert(debug_jump && !paused && !reading && ht.level==target);
            assert(ht.checkpoint==0 && ht.x==95*256 && !ht.puzzle.solved && ht.evidence==found);
            assert(ht.traversal.mode==HT_FREE && ht.traversal.ball_phase==0);
            ht_input(1);assert(!reading && ht.ticks==0);
            debug_jump=false;ht_select_level(ht.level);reports[source][0].buttons=0;ht_input(1);
        }
        press(source,select[source]);reports[source][0].buttons=0;ht_input(1);
        reports[source][0].hat=6;ht_input(1);assert(debug_select);
        reports[source][0].hat=8;ht_input(1);
        unsigned unchanged=ht.level;
        press(source,x[source]);assert(!debug_select && paused && !quitting && ht.level==unchanged);
        press(source,a[source]);assert(reading); // Original paused journal action survives.
        ht_journal_selection=1;
        reports[source][0].buttons=0;reports[source][0].hat=8;ht_input(1);
        reports[source][0].hat=0;ht_input(1);
        assert(ht_journal_selection==0 && reading);
        reports[source][0].hat=8;ht_input(1);
        reading=false;
        press(source,b[source]);assert(paused && !jump_down);
        ht_input(1);assert(paused && !jump_down); // Held B never repeats the mode change.
        reports[source][0].buttons=0;ht_input(1);
        reports[source][0].hat=0;ht_input(1);assert(paused && !jump_down);
        reports[source][0].hat=8;ht_input(1);
        reading=paused=false;
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
    /* Generic Up/Down are ladder intent nearby, including bottom Down:
     * neither accidental jump nor pause is synthesized at the ladder. */
    reading=paused=quitting=false;pause_down=jump_down=false;mapped=0;
    ht.level=1;ht_spawn(true);ht_geometry_level=1;ht.x=400*256;ht_input(1);
    mapped=T5_APP_BUTTON_DOWN;ht_input(1);
    assert((held&HT_DOWN) && !paused && !pause_down);
    mapped=0;ht_input(1);mapped=T5_APP_BUTTON_UP;ht_input(1);
    assert((held&HT_UP) && !jump_down && !reading);
    mapped=0;ht_input(1);ht.level=0;ht_spawn(true);ht_geometry_level=0;
    /* Generic keys: Up enters an existing trunk; sideways Up never captures,
     * Down descends, and A cannot switch an idle tree into a grab state. */
    ht_climb_tree tree;assert(ht_existing_tree(&ht,2,&tree));
    ht.x=ht_tree_edge(&tree,tree.base-3,1)*256;ht.y=(tree.base-3)*256;ht.grounded=true;
    mapped=0;ht_input(1);jump_down=pause_down=false;
    mapped=T5_APP_BUTTON_CONFIRM;ht_input(1);assert(ht.traversal.mode!=HT_TREE);
    mapped=0;ht_input(1);mapped=T5_APP_BUTTON_UP|T5_APP_BUTTON_RIGHT;ht_input(1);
    assert((held&HT_UP) && !jump_down);ht_step_controls(1,-1,false,false);assert(ht.traversal.mode!=HT_TREE);
    ht.x=ht_tree_edge(&tree,ht.y/256,1)*256;
    mapped=0;ht_input(1);mapped=T5_APP_BUTTON_UP;ht_input(1);
    assert((held&HT_UP) && !jump_down);ht_step_controls(0,-1,false,false);assert(ht.traversal.mode==HT_TREE);
    mapped=0;ht_input(1);mapped=T5_APP_BUTTON_DOWN;ht_input(1);assert((held&HT_DOWN) && !pause_down && !paused);
    mapped=0;ht_input(1);ht_spawn(true);
    /* A persistently failed provider hands back device input after a bounded
     * grace period, still requiring neutral before any mapped action. */
    reading=paused=quitting=false; mapped=0; ht_input(1);
    reports[0][0].connected=1; reports[0][0].device=99; reports[0][0].hat=8;
    ht_input(1); healthy=false; ht_input(1); now+=251; ht_input(1);
    mapped=0; ht_input(1); mapped=T5_APP_BUTTON_BACK; ht_input(1); assert(quitting);
    /* Real app scheduler: natural arrival begins the mill track once, locks
     * gameplay, then rearms neutral input without resetting progress. */
    reading=paused=quitting=loading=debug_jump=false;healthy=true;
    held=HT_RIGHT;previous=0;jump_down=pause_down=false;
    memset(&ht,0,sizeof(ht));ht.level=0;ht_spawn(true);ht_geometry_level=0;
    ht.x=1069*256;ht.y=ht_surface_at(&ht,2,1069)*256;ht.grounded=true;
    ht.traversal.forest_log_phase=32;ht.traversal.bridge_open=32;
    ht_cutscene.active=false;ht_cutscene_seen=0;simulation_started=false;
    ht_advance(now);
    for(int i=0;i<8 && !ht_cutscene.active;++i)ht_advance(now+=32);
    assert(ht_cutscene.active && ht_cutscene.id==HT_CUTSCENE_MILL);
    ht_game frozen=ht;
    for(int i=0;i<339;++i) {ht_advance(now+=32);assert(!memcmp(&ht,&frozen,sizeof(ht)));}
    ht_advance(now+=32);
    assert(!ht_cutscene.active && ht_input_rearm && held==0 && previous==0);
    assert(!memcmp(&ht,&frozen,sizeof(ht)));
    /* Real chapter transition launches the city track only after the gate
     * is solved/open. Geometry loading may pause timing; it cannot erase the track. */
    ht.level=0;ht_spawn(true);ht_geometry_level=0;ht_cutscene.active=false;
    ht.x=(HT_GOAL-1)*256;ht.y=ht_surface_at(&ht,9,HT_GOAL-1)*256;
    ht.puzzle.solved=true;ht.puzzle.opening=48;
    ht_input_rearm=false;held=HT_RIGHT;simulation_started=false;
    ht_advance(now);
    for(int i=0;i<8 && !ht_cutscene.active;++i)ht_advance(now+=32);
    assert(ht.level==1 && ht_cutscene.active && ht_cutscene.id==HT_CUTSCENE_CITY);
    frozen=ht;uint16_t tick=ht_cutscene.tick;
    ht_advance(now+=32);assert(ht_cutscene.tick==tick); /* geometry not ready */
    ht_geometry_level=1;simulation_started=false;ht_advance(now);
    for(int i=0;i<430;++i) {ht_advance(now+=32);assert(!memcmp(&ht,&frozen,sizeof(ht)));}
    assert(!ht_cutscene.active && ht_input_rearm && held==0 && previous==0);
    /* Held controller input cannot leak out of natural intro completion. */
    memset(reports,0,sizeof(reports));healthy=true;mapped=0;host_exit=false;
    reading=paused=quitting=loading=debug_jump=false;ht.level=0;ht_select_level(0);ht_spawn(true);
    ht_cutscene_begin(HT_CUTSCENE_INTRO);ht_cutscene.tick=HT_INTRO_TICKS-1;
    ht_geometry_level=0;simulation_started=false;ht_advance(now);
    held=HT_RIGHT|HT_JUMP;ht_advance(now+=32);
    assert(ht.x==95*256 && ht_input_rearm && !held && !jump_down);
    mapped=T5_APP_BUTTON_RIGHT|T5_APP_BUTTON_UP;ht_input(1);
    assert(!held && !jump_down && ht.x==95*256);
    mapped=0;ht_input(1);assert(!ht_input_rearm);
    mapped=T5_APP_BUTTON_RIGHT;ht_input(1);assert(held&HT_RIGHT);
    /* Exiting during the last segment remains immediate and does not apply
     * a second handoff or consume input as a gameplay action. */
    ht_cutscene_begin(HT_CUTSCENE_INTRO);ht_cutscene.tick=2140;
    host_exit=true;ht_input(1);assert(quitting && ht_cutscene.active);
    schoolroom_controls();
    motion_emphasis_controls();
    rain_controls();
    puts("Hollow Trail controls: receiver HID/XInput face labels, dedicated Start, A inspect, B jump, X back, arbitration and fault recovery PASS");
}
