/* The optional mountain rest must leave the authored route and final guards intact. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
#include "hollow_trail_route_walk.inc"

static uint32_t now,mapped;
static unsigned source;
static risc_usb_gamepad_state_v1 report;
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    now+=wait;memset(out,0,sizeof(*out));out->buttons=mapped;return true;
}
static uint32_t clock_ms(void){return now;}
static const t5_app_api_v1 fake_app={.abi_version=1,.struct_size=sizeof(fake_app),.poll=fake_poll,.millis=clock_ms};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &fake_app;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
static bool raw_poll(void *ctx,size_t n){(void)ctx;(void)n;return true;}
static bool snapshot(void *ctx,risc_usb_gamepad_state_v1 *out,size_t *count) {
    (void)ctx;assert(*count);*out=report;*count=1;return true;
}
static const risc_usb_gamepad_api_v1 gamepad={.api_version=1,.struct_size=sizeof(gamepad),.poll=raw_poll,.snapshot=snapshot};
static void input(uint32_t buttons,unsigned hat){report.buttons=buttons;report.hat=(uint8_t)hat;ht_input(1);}
static void action(void){input(0,8);input(source?2:1,8);}
static void cancel(void){input(0,8);input(source?4:8,8);}
static void journal(void){input(0,8);input(source?128:512,8);}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static void reset(void) {
    memset(&ht,0,sizeof(ht));ht.level=8;ht_select_level(8);ht_spawn(true);
    ht_shelter=(ht_shelter_state){0};ht_isolator=(ht_isolator_state){0};ht_tower=(ht_tower_state){0};ht_scarf=(ht_scarf_state){0};
    ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
    reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
    held=previous=mapped=0;ht_pad_owned=ht_input_rearm=ht_pad_fault=false;
    ht_pad_source=-1;simulation_started=false;simulation_accumulator=0;
    report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
    pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;app=&fake_app;
    walk_level=999;ht_input(1);input(0,8);
}
static void final_guards(void) {
    assert(!ht.verdict && !ht.last_verdict && !ht.endings && !ht.verdict_read && !ht.door_stage && !ht.laps);
    assert(!ht_decide(1) && !ht_decide(2) && !ht_door_begin());
}
static void approach_shelter(void) {
    input(0,2);
    for(unsigned step=0;step<512 && !ht_shelter_near(&ht);++step)ticks(1);
    input(0,8);
    assert(ht_shelter_near(&ht) && ht.grounded && ht.level==8 && !ht.deaths);
    assert(ht.x<ht_mech(&ht)->rope_anchor_x*256 && ht.traversal.mode==HT_FREE);
    assert(!ht.evidence && !ht.scene_evidence && !ht.puzzle.stage && !ht.puzzle.progress && !ht.puzzle.solved);
    final_guards();
}
static void optional_rest(unsigned mode) {
    ht_game frozen=ht;
    if(!mode)return; /* Walk straight past the reachable shelter. */
    action();input(0,8);
    assert(ht_shelter.active && !ht_shelter.stage && !reading);
    ticks(60);assert(!ht_shelter.stage && !memcmp(&ht,&frozen,sizeof(ht)));
    if(mode==1) {
        action();input(0,8);ticks(8);assert(ht_shelter.active && ht_shelter.stage==1);
        cancel();input(0,8);
        assert(!ht_shelter.active && !reading && !ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
        return;
    }
    for(unsigned stage=0;stage<14;stage+=2) {
        assert(ht_shelter.active && ht_shelter.stage==stage);
        if(stage==6) {
            journal();assert(reading && ht_journal_index && ht_shelter.active);
            unsigned tick=ht_shelter.tick;ticks(90);
            assert(ht_shelter.tick==tick && !memcmp(&ht,&frozen,sizeof(ht)));
            journal();input(0,8);assert(!reading && ht_shelter.stage==stage);
        }
        action();input(0,8);
        for(unsigned step=0;step<2048 && ht_shelter.stage==stage+1;++step)ticks(1);
        assert(ht_shelter.active && ht_shelter.stage==stage+2 && !memcmp(&ht,&frozen,sizeof(ht)));
        /* Waiting at a physical hold must not choose the next action. */
        ticks(90);assert(ht_shelter.stage==stage+2 && !memcmp(&ht,&frozen,sizeof(ht)));
    }
    action();input(0,8);
    assert(!ht_shelter.active && !reading && !ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
}
static void reach_ridge(void) {
    unsigned found=0,lines=0;bool rope=false;
    for(unsigned step=0;step<16000 && ht_puzzle_near(&ht)!=0;++step) {
        int item=ht_evidence_near(&ht);
        if(item>=0 && !ht_evidence_found(&ht,24u+(unsigned)item)) {
            assert(found<3 && (unsigned)item==found);
            if(!found)assert(rope && !ht_shelter.active); /* The scarf stays beyond the rope gap. */
            action();
            if(!found) {
                assert(ht_scarf.active && !reading && !ht_scarf.stage && !ht.scarf);
                journal();
            }
            assert(reading && journal_page==24u+found && ht_evidence_found(&ht,24u+found));
            ++found;cancel();if(reading)cancel();input(0,8);
            if(ht_scarf.active){cancel();input(0,8);}
            assert(!reading && !ht_shelter.active && !ht_isolator.active && !ht_scarf.active && !ht.scarf);
        }
        if(!rope && ht.traversal.mode==HT_FREE && ht_traversal_near(&ht)==HT_ROPE) {
            action();input(0,8);assert(ht.traversal.mode==HT_ROPE);
        }
        if(ht.traversal.mode==HT_ROPE)rope=true;
        if(ht.traversal.mode==HT_LADDER)lines|=1u<<ht.traversal.ladder;
        walk_route_tick();
        assert(ht.level==8 && !ht.deaths && !ht.puzzle.stage && !ht.puzzle.progress && !ht.puzzle.solved);
        assert(!ht_shelter.active && !ht_isolator.active);final_guards();
    }
    assert(ht_puzzle_near(&ht)==0 && found==3 && rope && lines==3 && walk_mechanics==14);
    assert(ht.evidence==(7u<<24) && ht.scene_evidence==7);
}
static void walk_to_station(unsigned station) {
    assert(station<4);
    for(unsigned step=0;step<2048 && ht_puzzle_near(&ht)!=(int)station;++step) {
        int target=HT_PUZZLE_FIRST+(int)station*HT_PUZZLE_SPACING;
        input(0,ht.x<target*256?2:6);ticks(1);
        assert(ht.level==8 && !ht.deaths && !reading && !ht_shelter.active);
    }
    input(0,8);assert(ht_puzzle_near(&ht)==(int)station);
}
static void unchanged_ridge_and_tower(void) {
    static const unsigned notes[]={1,0,2,1};
    walk_to_station(1);action();input(0,8);
    assert(!ht.puzzle.stage && !ht.puzzle.progress && !ht.puzzle.solved && ht.puzzle.feedback==HT_ECHO_CONNECTED);
    walk_to_station(3);action();input(0,8);
    assert(ht_isolator.active && ht.puzzle.stage==1 && !ht.puzzle.progress && !ht.puzzle.solved);
    cancel();input(0,8);assert(!ht_isolator.active && ht.puzzle.stage==1);
    for(unsigned strike=0;strike<4;++strike) {
        walk_to_station(notes[strike]);action();
        assert(ht.puzzle.progress==strike+1 && ht.puzzle.solved==(strike==3));
        for(unsigned step=0;step<30;++step){input(source?2:1,8);ticks(1);}
        assert(ht.puzzle.progress==strike+1);input(0,8);
    }
    assert(ht.puzzle.stage==1 && ht.puzzle.solved && ht.evidence==(7u<<24));final_guards();
    input(0,2);
    for(unsigned step=0;step<2048 && ht.level==8;++step)ticks(1);
    input(0,8);
    assert(ht.level==9 && !ht.deaths && !ht.puzzle.solved && !ht.puzzle.progress && !ht.scene_evidence);
    assert(ht.evidence==(7u<<24) && !ht_shelter.active && !ht_isolator.active && ht_final_locked(&ht));
    final_guards();
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8);
    assert(memory && bits);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
    for(source=0;source<2;++source)for(unsigned mode=0;mode<3;++mode) {
        reset();approach_shelter();optional_rest(mode);reach_ridge();unchanged_ridge_and_tower();
    }
    free(bits);free(memory);
    puts("Mountain route: optional shelter skip/cancel/complete, unchanged live state, rope, two safety lines, scarf/pages 24-26, isolated 1-0-2-1 ridge and locked tower guards on HID/XInput PASS");
    return 0;
}
