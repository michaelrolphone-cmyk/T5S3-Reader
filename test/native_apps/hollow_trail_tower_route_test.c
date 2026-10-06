/* Walk the tower's authored route, then use real input for its final choice. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
#include "hollow_trail_route_walk.inc"

static uint32_t now,mapped;
static risc_usb_gamepad_state_v1 report;
static unsigned source;
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    now+=wait;memset(out,0,sizeof(*out));out->buttons=mapped;return true;
}
static uint32_t clock_ms(void) {return now;}
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
static void input(uint32_t buttons,unsigned hat) {report.buttons=buttons;report.hat=(uint8_t)hat;ht_input(1);}
static void action(void){input(0,8);input(source?2:1,8);}
static void cancel(void){input(0,8);input(source?4:8,8);}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static void present(void){ht_journal_render();assert(ht_journal_page_ready);ht_read_submitted_revision=scene_revision;}
static void leave_inspection(void) {
    cancel();if(reading)cancel();input(0,8);
    assert(!reading && !ht_tower.active && !ht_input_rearm);
}
static void reset(void) {
    memset(&ht,0,sizeof(ht));ht.level=9;ht_select_level(9);ht_spawn(true);
    ht_tower=(ht_tower_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
    reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
    held=previous=mapped=0;ht_pad_owned=ht_input_rearm=ht_pad_fault=false;
    ht_pad_source=-1;simulation_started=false;
    report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
    pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;app=&fake_app;
    walk_level=999;ht_input(1);input(0,8);
}
static void card_location_guards(void) {
    assert(ht_evidence_platform(9,1)==8 && ht_evidence_x(9,1)==2755);
    ht_game g=ht;g.x=2755*256;g.y=ht_surface_at(&g,8,2755)*256;g.grounded=true;
    assert(ht_evidence_near(&g)==1 && ht_tower_card_near(&g));
    g.grounded=false;assert(ht_evidence_near(&g)<0 && !ht_tower_card_near(&g));
    g.grounded=true;g.y+=4*256;assert(ht_evidence_near(&g)<0 && !ht_tower_card_near(&g));
    g.y=ht_surface_at(&g,8,2755)*256;g.x=(2755+23)*256;
    assert(ht_evidence_near(&g)<0 && !ht_tower_card_near(&g));
    /* The former upper landing has no radio card to discover remotely. */
    g.x=1505*256;g.y=ht_surface_at(&g,4,1505)*256;
    assert(ht_evidence_near(&g)<0 && !ht_tower_card_near(&g));
    g=ht;g.x=2755*256;g.y=ht_surface_at(&g,7,2755)*256;g.grounded=true;
    assert(ht_evidence_near(&g)<0 && !ht_tower_card_near(&g));
}
static void reach_left_lamp(void) {
    static const unsigned expected[]={27,29,28};
    unsigned found=0;bool hooks=false,boat=false;
    for(unsigned step=0;step<12000 && ht_puzzle_near(&ht)!=0;++step) {
        if(!hooks && ht_tower_near(&ht)) {
            uint32_t evidence=ht.evidence;action();
            assert(ht_tower.active && !ht_tower.card && !reading && ht.evidence==evidence);
            leave_inspection();hooks=true;
        }
        int item=ht_evidence_near(&ht);
        if(item>=0 && !ht_evidence_found(&ht,27u+(unsigned)item)) {
            unsigned page=27u+(unsigned)item;
            assert(found<3 && page==expected[found++]);
            if(page==28) {
                assert(boat && ht.traversal.boat_x==2370*256);
                assert(ht_evidence_found(&ht,27) && ht_evidence_found(&ht,29));
                assert(ht.y==ht_surface_at(&ht,8,ht.x/256)*256);
            }
            action();assert(ht_evidence_found(&ht,page));
            if(page==28)assert(ht_tower.active && ht_tower.card && !reading);
            else assert(reading && journal_page==page && !ht_tower.active);
            leave_inspection();
        }
        if(ht.traversal.mode==HT_BOAT) {
            boat=true;assert(found==1 && !ht_evidence_found(&ht,28));
        }
        walk_route_tick();
        assert(ht.level==9 && !ht.deaths && !ht.puzzle.solved && !ht.puzzle.progress);
        assert(!ht.verdict && !ht.verdict_read && !ht.door_stage);
    }
    assert(hooks && boat && found==3 && walk_mechanics==20);
    assert(ht_puzzle_near(&ht)==0 && ht.evidence==(7u<<27));
    assert(ht.scene_evidence==7 && !ht_final_near(&ht));
    assert(!ht_decide(1) && !ht_decide(2) && !ht_door_begin());
}
static void three_distinct_strikes(void) {
    for(unsigned strike=1;strike<=3;++strike) {
        action();assert(ht.puzzle.progress==strike && ht.puzzle.solved==(strike==3));
        /* A held confirmation is one strike, irrespective of elapsed time. */
        for(unsigned held_tick=0;held_tick<60;++held_tick){input(source?2:1,8);ticks(1);}
        assert(ht.puzzle.progress==strike && !reading && !ht.verdict);
    }
    input(0,8);assert(ht.puzzle.solved && !ht.puzzle.opening && ht_final_locked(&ht));
    for(unsigned step=0;step<2000 && !ht_final_near(&ht);++step)ht_step_controls(1,0,false,false);
    assert(ht_final_near(&ht) && !ht.door_stage && !ht.deaths);
}
static void last_page(void) {
    for(unsigned page=0;page<64;++page) {
        present();if(ht_journal_next==ht_journal_length)return;
        input(0,8);input(0,2);
    }
    assert(!"testimony pagination exceeded the journal bound");
}
static void cabinet_and_read_guards(unsigned choice) {
    action();assert(reading && ht_journal_deciding && !ht_journal_confirm);
    if(choice==2){input(0,8);input(0,2);}
    action();assert(!ht_journal_confirm && !ht.verdict); /* Nothing submitted. */
    present();action();assert(ht_journal_confirm && !ht.verdict);
    action();assert(!ht.verdict); /* Confirmation itself must be rendered. */
    present();++scene_revision;action();assert(!ht.verdict); /* Stale frame. */
    present();action();
    assert(ht.verdict==choice && ht.last_verdict==choice && ht.endings==(1u<<(choice-1)));
    assert(reading && !ht_journal_deciding && journal_page==29 && !ht.verdict_read);
    assert(!ht_decide(3-choice) && !ht_decide(choice) && ht_final_locked(&ht));
    ht_game frozen=ht;input(0,8);ticks(120);assert(!memcmp(&ht,&frozen,sizeof(ht)));
    action();assert(ht_journal_index && !ht.verdict_read);cancel();input(0,8);
    /* The solved lamps and chosen release still cannot bypass unread testimony. */
    for(unsigned step=0;step<500;++step)ht_step_controls(1,0,false,false);
    assert(!ht.door_stage && !ht_door_begin() && ht.x<HT_GOAL*256);
    action();assert(reading && journal_page==29);present();
    assert(!strcmp(ht_journal_body,ht_final_testimony[choice-1]));
    assert(ht_journal_next<ht_journal_length); /* A real multi-page reading. */
    action();assert(!ht.verdict_read && ht_journal_index);cancel();input(0,8);
    action();last_page();++scene_revision;action();
    assert(!ht.verdict_read && ht_journal_index);cancel();input(0,8);
    action();last_page();action();assert(!reading && ht.verdict_read && !ht_final_locked(&ht));
    input(0,8);
    for(unsigned step=0;step<500 && !ht.door_stage;++step)ht_step_controls(1,0,false,false);
    assert(ht.door_stage==HT_DOOR_YARD && ht.x==32*256 && !ht.laps);
    assert(ht.evidence==(7u<<27) && ht.verdict==choice && ht.endings==(1u<<(choice-1)));
    assert(!ht_decide(3-choice));
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8);
    assert(memory && bits);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
    for(source=0;source<2;++source)for(unsigned choice=1;choice<=2;++choice) {
        reset();card_location_guards();reach_left_lamp();three_distinct_strikes();cabinet_and_read_guards(choice);
    }
    free(bits);free(memory);
    puts("Tower route: optional hooks, boat crossing, page27/29/28 order, upper radio card guards, three released left strikes, exclusive choices and submitted final-page door guard on HID/XInput PASS");
    return 0;
}
