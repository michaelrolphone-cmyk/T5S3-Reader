/* Earn the two garden counts through the authored route and real app input. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
#include "hollow_trail_route_walk.inc"

static uint32_t now,mapped;
static bool host_exit,pad_failure;
static unsigned source;
static risc_usb_gamepad_state_v1 report;
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    now+=wait;memset(out,0,sizeof(*out));out->buttons=mapped;out->exit_requested=host_exit;return true;
}
static uint32_t clock_ms(void){return now;}
static const t5_app_api_v1 fake_app={.abi_version=1,.struct_size=sizeof(fake_app),.poll=fake_poll,.millis=clock_ms};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &fake_app;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
static bool raw_poll(void *ctx,size_t n){(void)ctx;(void)n;return !pad_failure;}
static bool snapshot(void *ctx,risc_usb_gamepad_state_v1 *out,size_t *count){
    (void)ctx;assert(*count);*out=report;*count=1;return true;
}
static const risc_usb_gamepad_api_v1 gamepad={.api_version=1,.struct_size=sizeof(gamepad),.poll=raw_poll,.snapshot=snapshot};
static uint32_t a_button(void){return source?2u:1u;}
static void input(uint32_t buttons,unsigned hat){report.buttons=buttons;report.hat=(uint8_t)hat;ht_input(1);}
static void press(uint32_t mask){input(0,8);input(mask,8);}
static void action(void){press(a_button());}
static void cancel(void){press(source?4:8);}
static void journal(void){press(source?128:512);}
static void pause_study(void){press(source?64:256);}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static void present(void){ht_journal_render();assert(ht_journal_page_ready);ht_read_submitted_revision=scene_revision;}
static void same_position(const ht_counts_state *before){
    assert(ht_counts.stage==before->stage && ht_counts.tick==before->tick);
    assert(ht_counts.dx==before->dx && ht_counts.dy==before->dy);
}
static void quiet(unsigned count){
    input(0,8);ht_counts_state before=ht_counts;unsigned revision=scene_revision;
    ticks(count);same_position(&before);assert(scene_revision==revision);
}
static void reset(void){
    memset(&ht,0,sizeof(ht));ht.level=6;ht_select_level(6);ht_spawn(true);
    ht_counts=(ht_counts_state){0};ht_sleep=(ht_sleep_state){0};
    ht_partition=(ht_partition_state){0};ht_hoist=(ht_hoist_state){0};
    ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
    reading=paused=quitting=loading=debug_jump=debug_select=jump_down=pause_down=false;
    ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
    ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;ht_pad_device=0;
    simulation_started=false;simulation_accumulator=0;
    report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
    pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;
    host_exit=pad_failure=false;app=&fake_app;
    /* Stop before the route witness inspects this item: entry must acquire it. */
    walk_level=999;
    for(unsigned i=0;i<20000 && !ht_counts_near(&ht);++i)walk_route_tick();
    assert(ht.level==6 && ht_counts_near(&ht) && !ht.deaths);
    assert(!ht_evidence_found(&ht,19) && !ht.puzzle.solved && !ht.puzzle.stage);
    ht_input(1);input(0,8);
}
static ht_game enter(void){
    ht_game before=ht;action();
    assert(ht_counts.active && !ht_counts.stage && !reading && ht_input_rearm);
    assert(ht_evidence_found(&ht,19));
    before.evidence|=1u<<19;before.scene_evidence|=2;
    assert(!memcmp(&ht,&before,sizeof(ht)));
    /* The entry press, even held, cannot also take the sheets. */
    for(unsigned i=0;i<8;++i)input(a_button(),8);
    assert(!ht_counts.stage && ht_input_rearm);
    input(0,8);assert(!ht_input_rearm);
    return ht;
}
static void move_to(int dx,int dy){
    for(unsigned guard=0;guard<256 && ht_counts.dx!=dx;++guard){
        input(0,ht_counts.dx<dx?2:6);ticks(1);
    }
    input(0,8);assert(ht_counts.dx==dx);
    for(unsigned guard=0;guard<128 && ht_counts.dy!=dy;++guard){
        input(0,ht_counts.dy<dy?4:0);ticks(1);
    }
    input(0,8);assert(ht_counts.dy==dy);
}
static void interruption(void){
    pause_study();assert(ht_counts.paused);ht_counts_state before=ht_counts;
    input(a_button(),2);ticks(40);same_position(&before);
    pause_study();assert(!ht_counts.paused);input(0,8);
    journal();assert(reading && ht_counts.active && journal_page==19);
    before=ht_counts;ticks(40);same_position(&before);
    journal();input(0,8);assert(!reading && ht_counts.active);
    input(0,2);ticks(2);
    pad_failure=true;input(0,2);assert(ht_pad_fault && ht_input_rearm);
    before=ht_counts;ticks(40);same_position(&before);
    /* Recovery with a held direction must wait for neutral, not replay it. */
    pad_failure=false;input(0,2);ticks(8);same_position(&before);assert(ht_input_rearm);
    input(0,8);assert(!ht_input_rearm && !ht_pad_fault);
    /* A replacement controller also requires its own neutral report. */
    ++report.device;input(a_button(),2);ticks(8);same_position(&before);assert(ht_input_rearm);
    input(0,8);assert(!ht_input_rearm);
    /* A real disconnect and reconnection cannot inject the held action. */
    report.connected=0;input(0,8);before=ht_counts;
    report.connected=1;input(a_button(),8);ticks(8);same_position(&before);assert(ht_input_rearm);
    input(0,8);assert(!ht_input_rearm);
}
static void state_machine(void){
    ht_counts_begin();ht_counts_state state=ht_counts;
    assert(state.active && !state.stage && state.dx==34 && state.dy==-16);
    assert(ht_counts_action(&state) && state.stage==1 && !state.tick);
    for(unsigned tick=0;tick<159;++tick){assert(ht_counts_step(&state));assert(state.stage==1);}
    assert(ht_counts_step(&state) && state.stage==2);
    assert(!ht_counts_action(&state));
    state.move_x=-1;state.move_y=1;
    for(unsigned i=0;i<16;++i)assert(ht_counts_step(&state));
    assert(state.dx==18 && !state.dy);
    state.move_y=0;
    for(unsigned i=0;i<18;++i)assert(ht_counts_step(&state));
    assert(!state.dx && !state.dy);
    state.move_x=1;assert(ht_counts_step(&state));assert(!ht_counts_action(&state));
    state.move_x=-1;assert(ht_counts_step(&state));
    state.move_x=0;state.move_y=-1;assert(ht_counts_step(&state));assert(!ht_counts_action(&state));
    state.move_y=1;assert(ht_counts_step(&state));
    state.move_y=0;assert(ht_counts_action(&state) && state.stage==3);
    for(unsigned i=0;i<10000;++i)assert(!ht_counts_step(&state));
    assert(ht_counts_action(&state) && state.stage==4 && !state.tick);
    for(unsigned tick=0;tick<159;++tick){assert(ht_counts_step(&state));assert(state.stage==4);}
    assert(ht_counts_step(&state) && state.stage==5);
    for(unsigned i=0;i<10000;++i)assert(!ht_counts_step(&state));
    assert(!ht_counts_action(&state));
    state=(ht_counts_state){.active=true,.stage=2,.move_x=1,.move_y=1};
    for(unsigned i=0;i<128;++i)(void)ht_counts_step(&state);
    assert(state.dx==48 && state.dy==24 && !ht_counts_step(&state));
    state.move_x=-1;state.move_y=-1;
    for(unsigned i=0;i<128;++i)(void)ht_counts_step(&state);
    assert(state.dx==-48 && state.dy==-24 && !ht_counts_step(&state));
    state.paused=true;ht_counts_state before=state;
    assert(!ht_counts_step(&state) && !ht_counts_action(&state) && !memcmp(&state,&before,sizeof(state)));
    state.active=false;state.paused=false;before=state;
    assert(!ht_counts_step(&state) && !ht_counts_action(&state) && !memcmp(&state,&before,sizeof(state)));
}
static void controls(void){
    reset();ht_game frozen=enter();quiet(80);
    action();assert(ht_counts.stage==1);input(0,8);ticks(170);
    assert(ht_counts.stage==2 && ht_counts.dx==34 && ht_counts.dy==-16);
    action();assert(ht_counts.stage==2);quiet(80);
    interruption();assert(ht_counts.stage==2 && !reading);
    input(0,3);ticks(128);input(0,8);assert(ht_counts.dx==48 && ht_counts.dy==24);quiet(80);
    input(0,7);ticks(128);input(0,8);assert(ht_counts.dx==-48 && ht_counts.dy==-24);quiet(80);
    move_to(1,0);action();assert(ht_counts.stage==2);
    move_to(0,-1);action();assert(ht_counts.stage==2);
    move_to(0,0);quiet(80);action();assert(ht_counts.stage==3);quiet(80);
    assert(!memcmp(&ht,&frozen,sizeof(ht)));
    action();assert(ht_counts.stage==4);input(0,8);ticks(170);
    assert(ht_counts.stage==5);quiet(80);
    assert(!memcmp(&ht,&frozen,sizeof(ht)));
    action();assert(reading && journal_page==19 && ht_counts.active);
    /* Completing this physical evidence note cannot satisfy the ending read. */
    for(unsigned leaf=0;;++leaf){
        assert(leaf<64);present();if(ht_journal_next==ht_journal_length)break;
        input(0,8);input(0,2);
    }
    action();assert(ht_journal_index && reading && !ht.verdict_read);
    assert(!memcmp(&ht,&frozen,sizeof(ht)));
    journal();input(0,8);assert(!reading);cancel();
    assert(!ht_counts.active && !quitting && ht_input_rearm);
    input(a_button(),2);assert(!ht_counts.active && ht_input_rearm);
    input(0,8);action();assert(ht_counts.active && !ht_counts.stage);
    input(0,8);action();assert(ht_counts.stage==1);input(0,8);ticks(40);
    pause_study();ht_counts_state before=ht_counts;ticks(40);same_position(&before);
    pause_study();input(0,8);ticks(170);assert(ht_counts.stage==2);
    cancel();assert(!ht_counts.active);input(0,8);action();assert(!ht_counts.stage);
    host_exit=true;input(0,8);assert(quitting);
}
static void interruption_each_stage(void){
    for(unsigned stage=0;stage<=5;++stage){
        reset();ht_game frozen=enter();
        ht_counts.stage=(uint8_t)stage;ht_counts.tick=55;
        pause_study();assert(ht_counts.paused);ht_counts_state before=ht_counts;
        input(a_button(),4);ticks(10);same_position(&before);
        journal();assert(reading && ht_counts.active);ticks(10);same_position(&before);
        journal();input(0,8);assert(!reading && ht_counts.paused);
        cancel();assert(!ht_counts.active && !quitting && ht_input_rearm);
        assert(!memcmp(&ht,&frozen,sizeof(ht)));
        input(a_button(),2);ticks(2);assert(!ht_counts.active && ht_input_rearm);
        input(0,8);action();assert(ht_counts.active && !ht_counts.stage);
    }
}
static void rendering(uint8_t *copy,uint8_t *bits,uint8_t *base){
    reset();ht_game frozen=enter();
    ht_game far=frozen;far.x-=100*256;assert(!ht_counts_near(&far));
    far=frozen;far.grounded=false;assert(!ht_counts_near(&far));
    far=frozen;far.traversal.mode=HT_LADDER;assert(!ht_counts_near(&far));
    far=frozen;far.level=5;assert(!ht_counts_near(&far));
    for(unsigned native=0;native<2;++native){
        ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        unsigned bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
        for(unsigned stage=0;stage<=5;++stage){
            ht_counts_state state={.active=true,.dx=34,.dy=-16,.stage=(uint8_t)stage,.tick=80};
            if(stage>=3)state.dx=state.dy=0;
            ht_counts_study_render(&frozen,&state);memcpy(copy,ht_scene,bytes);ht_pack_mono(base,120);
            ht_counts_state saved=state;
            memset(ht_scene,255,bytes);ht_counts_study_render(&frozen,&state);ht_pack_mono(bits,120);
            assert(!memcmp(copy,ht_scene,bytes) && !memcmp(base,bits,HT_NATIVE_PIXELS/8));
            assert(!memcmp(&state,&saved,sizeof(state)) && !memcmp(&ht,&frozen,sizeof(ht)));
        }
        ht_counts_state state={.active=true,.dx=34,.dy=-16,.stage=2};
        ht_counts_study_render(&frozen,&state);memcpy(copy,ht_scene,bytes);ht_pack_mono(base,120);
        state.dx=state.dy=0;ht_counts_study_render(&frozen,&state);ht_pack_mono(bits,120);
        assert(memcmp(copy,ht_scene,bytes));
        /* Count only the image above the caption: visible alignment, not text. */
        unsigned changed=0;
        for(unsigned i=0;i<120*440;++i)changed+=base[i]!=bits[i];
        assert(changed>20);
        for(unsigned stage=0;stage<=5;++stage)for(unsigned tick=0;tick<=160;++tick){
            state.stage=(uint8_t)stage;state.tick=(uint16_t)tick;
            ht_person_pose pose=ht_counts_pose(&frozen,&state);
            for(unsigned side=0;side<2;++side){
                int dx=pose.hand[side].x-pose.shoulder.x,dy=pose.hand[side].y-pose.shoulder.y;
                if(dx*dx+dy*dy>144)fprintf(stderr,"counts hand stage=%u tick=%u side=%u dx=%d dy=%d\n",stage,tick,side,dx,dy);
                assert(dx*dx+dy*dy<=144);
                dx=pose.foot[side].x-pose.hip.x;dy=pose.foot[side].y-1-pose.hip.y;
                if(dx*dx+dy*dy>225)fprintf(stderr,"counts foot stage=%u tick=%u side=%u dx=%d dy=%d\n",stage,tick,side,dx,dy);
                assert(dx*dx+dy*dy<=225);
            }
        }
        state.stage=1;state.tick=64;
        ht_person_pose clip=ht_counts_pose(&frozen,&state);
        for(unsigned side=0;side<2;++side){
            assert(clip.hand[side].x+frozen.camera/256==ht_counts_x()+(side?4:-10));
            assert(clip.hand[side].y+frozen.camera_y/256==ht_counts_floor()-25);
        }
        state.stage=4;state.tick=128;
        ht_person_pose pot=ht_counts_pose(&frozen,&state);
        for(unsigned side=0;side<2;++side){
            assert(pot.hand[side].x+frozen.camera/256==ht_counts_x()+(side?6:-3));
            assert(pot.hand[side].y+frozen.camera_y/256==ht_counts_floor()-16-(side?4:0));
        }
        assert(!memcmp(&ht,&frozen,sizeof(ht)));
    }
}
int main(void){
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
    uint8_t *bits=malloc(HT_NATIVE_PIXELS/8),*base=malloc(HT_NATIVE_PIXELS/8);
    assert(memory&&copy&&bits&&base);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
    state_machine();
    for(source=0;source<2;++source){controls();interruption_each_stage();}
    source=0;rendering(copy,bits,base);
    free(base);free(bits);free(copy);free(memory);
    puts("Garden counts: actual route, bounded reversible exact alignment, quiet holds, physical return, journal/ending guard, frozen world, HID/XInput pause/fault/neutral/retry/exit, deterministic gray/mono and limb reach PASS");
}
