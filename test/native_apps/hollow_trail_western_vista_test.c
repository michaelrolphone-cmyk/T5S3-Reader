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
static unsigned checkpoints;
static void checkpoint(void){++checkpoints;}
static void western_controls(void) {
    for(unsigned source=0;source<2;++source) {
        memset(reports,0,sizeof(reports));reports[source][0].connected=1;
        reports[source][0].device=source+1;reports[source][0].hat=8;
        healthy=true;host_exit=false;app=&fake_app;pad=&xapi;hid_pad=&hapi;
        memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
        ht.x=HT_SIGNAL_LOG_X*256;ht.y=-100*256;ht.grounded=true;
        assert(ht_inspect()==5); /* Earn the log using its production action. */
        ht.x=(HT_SIGNAL_ROOM_RIGHT-2)*256;ht.camera=2080*256;ht.camera_y=-280*256;
        ht_cutscene.active=false;ht_cutscene_seen=0;
        reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
        ht_schoolroom_studying=ht_signal_room_studying=false;
        held=previous=mapped=0;ht_pad_owned=ht_input_rearm=false;
        ht_pad_source=-1;simulation_started=false;ht_input(1);
        reports[source][0].hat=2;ht_input(1);
        for(unsigned i=0;i<8&&!ht_cutscene.active;++i)ht_advance(now+=HT_STEP_MS);
        assert(ht_cutscene.active&&ht_cutscene.id==HT_CUTSCENE_WEST);
        ht_game retained=ht;
        /* Holding direction, A and Start cannot advance or walk out of it. */
        reports[source][0].buttons=source?130:513;ht_input(1);
        assert(!reading&&!paused&&!quitting);
        while(ht_cutscene.active)ht_advance(now+=HT_STEP_MS);
        assert(!memcmp(&ht,&retained,sizeof(ht))&&ht_input_rearm&&!held);
        ht_input(1);assert(ht_input_rearm&&!held);
        reports[source][0].buttons=0;reports[source][0].hat=8;ht_input(1);
        assert(!ht_input_rearm);
        reports[source][0].hat=2;ht_input(1);ht_advance(now+=HT_STEP_MS);
        assert(ht.x>retained.x&&!ht_cutscene.active);
        ht_cutscene_begin(HT_CUTSCENE_WEST);host_exit=true;ht_input(1);
        assert(quitting);host_exit=false;ht_cutscene.active=false;
    }
}
int main(void) {
    uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
    uint8_t *bits=malloc(HT_NATIVE_PIXELS/8),*other=malloc(HT_NATIVE_PIXELS/8);
    assert(mem&&copy&&bits&&other);ht_bind(mem);ht_bind_native(mem);ht_service=checkpoint;
    for(unsigned native=0;native<2;++native) {
        ht_native_active=native!=0;ht_native_foreground_half_y=false;
        ht_scene=native?ht_native_a:ht_scene_low;ht_world_scale=256;
        size_t bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
        memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
        ht_game retained=ht;
        for(unsigned step=0;step<=256;step+=32) {
            checkpoints=0;memset(ht_scene,0,bytes);ht_western_vista_draw(step);memcpy(copy,ht_scene,bytes);
            assert(checkpoints>10&&checkpoints<128);
            memset(ht_scene,255,bytes);ht_western_vista_draw(step);
            assert(!memcmp(copy,ht_scene,bytes));assert(!memcmp(&ht,&retained,sizeof(ht)));
        }
        ht_western_vista_draw(0);ht_pack_mono(bits,120);
        ht_western_vista_draw(256);ht_pack_mono(other,120);
        assert(memcmp(bits,other,HT_NATIVE_PIXELS/8));
    }
    for(unsigned native=0;native<2;++native) {
        ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
        ht.x=(HT_SIGNAL_ROOM_RIGHT-1)*256;ht.y=-100*256;ht.grounded=true;
        ht.camera=2080*256;ht.camera_y=-280*256;ht.intimacy=157;ht.vista=23;
        ht.sway_phase=134;ht.rotation_phase=128u<<8;ht.drop_zoom=103;ht_cutscene_seen=0;
        ht_game before=ht;ht.x=HT_SIGNAL_ROOM_RIGHT*256;
        assert(!ht_cutscene_western_departure(&before,&ht));
        ht.evidence|=1u<<5;
        assert(ht_cutscene_western_departure(&before,&ht));
        ht_game valid=ht;
        ht.y=70*256;assert(!ht_cutscene_western_departure(&before,&ht));ht=valid;
        ht.grounded=false;assert(!ht_cutscene_western_departure(&before,&ht));ht=valid;
        before.x-=20*256;assert(!ht_cutscene_western_departure(&before,&ht));before.x+=20*256;
        ht_game retained=ht;ht_cutscene_begin(HT_CUTSCENE_WEST);
        assert(!ht_cutscene_western_departure(&before,&ht));
        ht_render_scene_from(&ht,true);size_t bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
        memcpy(copy,ht_scene,bytes);
        for(unsigned tick=0;tick<=640;tick+=32) {
            ht_cutscene.tick=tick;ht_cutscene_render(&ht_cutscene);
            assert(!memcmp(&ht,&retained,sizeof(ht)));
            if(tick==0 || tick==640)assert(!memcmp(copy,ht_scene,bytes));
        }
        /* Independent inputs catch a dissolve that accidentally overwrites
         * its saved world image when the ordinary camera uses native_b. */
        uint8_t *vista=malloc(bytes);assert(vista);
        ht_world_scale=256;ht_western_vista_draw(48u*256u/640u);ht_vignette();
        memcpy(vista,ht_scene,bytes);ht_cutscene.tick=48;ht_cutscene_render(&ht_cutscene);
        for(size_t i=0;i<bytes;++i)assert(ht_scene[i]==(copy[i]*128u+vista[i]*128u)/256u);
        free(vista);
        ht_cutscene.tick=639;assert(ht_cutscene_step(&ht_cutscene));
        ht_cutscene_apply_handoff(&ht_cutscene);assert(!memcmp(&ht,&retained,sizeof(ht)));
    }
    ht_service=NULL;western_controls();
    free(other);free(bits);free(copy);free(mem);
    puts("Western panorama: complete deterministic writes, both rasters, bounded checkpoints, parallax in mono and unchanged game state PASS");
}
