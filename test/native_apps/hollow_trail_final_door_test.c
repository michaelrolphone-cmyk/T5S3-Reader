/* Chapter XI drives the real app input, journal, simulator and raster paths. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
static uint32_t now,mapped;
static bool host_exit;
static risc_usb_gamepad_state_v1 report;
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    now+=wait;memset(out,0,sizeof(*out));out->buttons=mapped;out->exit_requested=host_exit;return true;
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
static unsigned source;
static void input(uint32_t buttons,unsigned hat) {report.buttons=buttons;report.hat=(uint8_t)hat;ht_input(1);}
static void press(uint32_t mask){input(0,8);input(mask,8);}
static void action(void){press(source?2:1);}
static void cancel(void){press(source?4:8);}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static void present(void){ht_journal_render();assert(ht_journal_page_ready);ht_read_submitted_revision=scene_revision;}
static void reset(unsigned choice) {
    memset(&ht,0,sizeof(ht));ht.level=HT_LEVELS-1;ht_select_level(ht.level);ht_spawn(true);
    ht.puzzle.solved=true;ht.puzzle.opening=48;ht.x=HT_GOAL*256;
    ht.y=ht_land[9].top*256;ht.grounded=true;ht.verdict=(uint8_t)choice;ht.verdict_read=true;
    ht.last_verdict=(uint8_t)choice;ht.endings=(uint8_t)(1u<<(choice-1));ht.evidence=(1u<<30)-1;
    ht_cutscene.active=ht_cutscene.finished=false;
    reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
    ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
    ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
    report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
    pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=false;
    app=&fake_app;ht_input(1);ticks(1);
    assert(ht.door_stage==HT_DOOR_YARD && ht.level==HT_LEVELS-1 && !ht.laps && ht_input_rearm);
    ht_world_scale=HT_DOOR_SCALE;
    assert(ht_project_x(ht.x/256-ht.camera/256)>32);
    ht_world_scale=256;
    input(0,8);
}
static void walk_to(int x) {
    input(0,2);
    unsigned n=0;while(ht.x/256<x && n++<1600)ticks(1);
    assert(n<1600);input(0,8);ticks(8);
    assert(ht.y==ht_door_ground(ht.x/256)*256 && ht.grounded);
}
static void last_page(void) {
    for(unsigned page=0;;++page) {
        assert(page<63);present();if(ht_journal_next==ht_journal_length)break;
        input(0,8);input(0,2);
    }
}
static unsigned checkpoints;
static void checkpoint(void){++checkpoints;}
static void contact_checks(void) {
    ht_game g=ht;
    for(unsigned stage=HT_DOOR_REST;stage<=HT_DOOR_WAIT;++stage) {
        if(stage==HT_DOOR_STREET)continue;
        g.door_stage=(uint8_t)stage;
        for(unsigned tick=0;tick<480;++tick) {
            g.door_tick=(uint16_t)tick;
            ht_person_pose p=ht_door_pose(&g);
            for(unsigned side=0;side<2;++side) {
                int dx=p.hand[side].x-p.shoulder.x,dy=p.hand[side].y-p.shoulder.y;
                assert(dx*dx+dy*dy<=12*12); /* Shared 6+6 arm, no stretched reach. */
                dx=p.foot[side].x-p.hip.x;dy=p.foot[side].y-1-p.hip.y;
                assert(dx*dx+dy*dy<=15*15); /* Shared 8+7 leg. */
            }
        }
    }
}
static void render_checks(uint8_t *prior,uint8_t *bits) {
    ht_game saved=ht;
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;uint8_t *out=mode?ht_native_a:ht_scene_low;
        memset(out,0,bytes);ht_framed=true;ht_service=checkpoint;checkpoints=0;ht_render_scene();ht_narration(&ht);
        assert(checkpoints>10 && ht_world_scale==256 && !ht_framed);ht_service=NULL;
        memcpy(prior,out,bytes);ht_pack_mono(bits,120);
        uint8_t packed[HT_NATIVE_PIXELS/8];memcpy(packed,bits,sizeof(packed));
        memset(out,255,bytes);ht_render_scene();ht_narration(&ht);assert(!memcmp(prior,out,bytes));
        ht_pack_mono(bits,120);assert(!memcmp(packed,bits,sizeof(packed)));
        assert(!memcmp(&ht,&saved,sizeof(ht)));
    }
    /* Shoes, hands, ground and full coat all remain above the narration band. */
    ht_world_scale=HT_DOOR_SCALE;
    assert(ht_project_y(ht.y/256-ht.camera_y/256)+8<230);
    ht_world_scale=256;
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*prior=malloc(HT_NATIVE_PIXELS);
    uint8_t *bitmap=malloc(HT_NATIVE_PIXELS/8);assert(memory && prior && bitmap);
    ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bitmap;
    for(source=0;source<2;++source)for(unsigned choice=1;choice<=2;++choice) {
        reset(choice);int x=ht.x; ticks(200);assert(ht.x==x && ht.door_stage==HT_DOOR_YARD);
        action();assert(ht.door_stage==HT_DOOR_YARD); /* Too far away. */
        walk_to(HT_DOOR_BARREL+30);ticks(120);assert(ht.x/256<=HT_DOOR_BARREL+30);
        action();assert(ht.door_stage==HT_DOOR_REST && ht_input_rearm);
        input(0,8);ticks(90);
        press(source?64:256);ticks(1);assert(paused);unsigned at=ht.door_tick;
        ticks(300);assert(ht.door_tick==at);press(source?64:256);ticks(1);assert(!paused);
        input(0,2); /* Holding walk cannot leak through the earned rest. */
        while(ht.door_stage==HT_DOOR_REST)ticks(1);
        assert(ht.door_stage==HT_DOOR_STREET && ht_input_rearm && !held);
        x=ht.x;ticks(30);assert(ht.x==x);input(0,8);
        walk_to(HT_DOOR_STOP_X);assert(ht.door_stage==HT_DOOR_STREET);
        action();assert(ht.door_stage==HT_DOOR_SHOES);input(0,8);
        int still_camera=ht.camera,still_camera_y=ht.camera_y;
        for(unsigned i=0;i<HT_DOOR_SHOES_TICKS+2 && ht.door_stage==HT_DOOR_SHOES;++i)ticks(1);
        assert(ht.door_stage==HT_DOOR_NOTEBOOK);ticks(500);assert(ht.door_stage==HT_DOOR_NOTEBOOK && !ht.door_notebook);
        input(0,8);action();assert(reading && journal_page==HT_DOOR_JOURNAL);
        action();assert(ht_journal_index && !ht.door_notebook);cancel();assert(!reading);
        action();assert(reading);present();assert(strstr(ht_journal_body,"I still want to find her."));
        assert(strstr(ht_journal_body,"She had been trying to stay where someone could see her."));
        assert(strstr(ht_journal_body,choice==1?"I broke the feed because I thought it would free her.":"I completed the circuit because I believed she knew what she was doing."));
        ht_game frozen=ht;ticks(500);assert(!memcmp(&ht,&frozen,sizeof(ht)));
        last_page();++scene_revision;action();assert(!ht.door_notebook); /* Unsubmitted page. */
        cancel();action();last_page();action();
        assert(!reading && ht.door_notebook && ht.door_stage==HT_DOOR_GRIEF && ht_input_rearm);
        input(0,8);
        for(unsigned i=0;i<HT_DOOR_GRIEF_TICKS+HT_DOOR_STRAIGHTEN_TICKS+HT_DOOR_KNOCK_TICKS+6 && ht.door_stage!=HT_DOOR_WAIT;++i)ticks(1);
        assert(ht.door_stage==HT_DOOR_WAIT && ht.laps==1 && ht.verdict==choice && ht.verdict_read);
        assert(ht.camera==still_camera && ht.camera_y==still_camera_y);
        assert(ht.level==HT_LEVELS-1 && ht.evidence==((1u<<30)-1));
        frozen=ht;uint32_t revision=scene_revision;
        input(0,8);input(source?9:6,2);ticks(20000);assert(!memcmp(&ht,&frozen,sizeof(ht)) && scene_revision==revision);
        action();assert(!reading && !memcmp(&ht,&frozen,sizeof(ht)));
        ht_spawn(false);assert(!memcmp(&ht,&frozen,sizeof(ht)));contact_checks();render_checks(prior,bitmap);
        press(source?128:512);assert(reading);cancel();assert(!reading);ticks(10);assert(!memcmp(&ht,&frozen,sizeof(ht)));
        unsigned ids[HT_JOURNAL_RECORDS],count=ht_journal_list(ids);assert(ids[count-1]==HT_DOOR_JOURNAL);
        host_exit=true;ht_input(1);assert(quitting);
    }
    /* Entrance cannot be manufactured by time, a wrong chapter, or unread testimony. */
    reset(1);ht.door_stage=0;ht.x=HT_GOAL*256;ht.verdict_read=false;assert(!ht_door_begin());
    ht.verdict_read=true;ht.level=8;assert(!ht_door_begin());ht.level=9;ht.grounded=false;assert(!ht_door_begin());
    ht.grounded=true;ht.puzzle.opening=47;assert(!ht_door_begin());
    /* A chapter jump cannot insert an unvisited schoolroom memory. */
    ht.evidence=0;ht.door_stage=HT_DOOR_NOTEBOOK;ht_journal_open(HT_DOOR_JOURNAL);present();
    assert(!strstr(ht_journal_body,"schoolroom") && !strstr(ht_journal_body,"stay where someone could see her"));
    /* Explicit chapter replay is still available; it is never automatic. */
    ht.level=0;ht_spawn(true);assert(!ht.door_stage && !ht.door_notebook && ht.level==0);
    free(bitmap);free(prior);free(memory);
    puts("Final door: both controller sources/choices, earned pauses, physical walking, notebook pagination/cancel, static end, both rasters and exit PASS");
}
