/* The production render checkpoints must advance gameplay while preserving
 * one coherent displayed snapshot. No controller setup/mapping changes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
static uint32_t fake_now,clock_increment;
static bool schedule;
static unsigned yielding_polls,nowait_polls;
static bool request_exit;
static uint32_t forced_buttons;
static uint32_t fake_millis(void) { fake_now+=clock_increment; return fake_now; }
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    ++yielding_polls;
    fake_now+=wait;
    *out=(t5_app_input_t){.buttons=schedule && fake_now>=20 && fake_now<110?T5_APP_BUTTON_RIGHT:forced_buttons};
    return true;
}
static bool fake_nowait(t5_app_input_t *out) {
    ++nowait_polls;
    *out=(t5_app_input_t){.buttons=forced_buttons,.exit_requested=request_exit};
    return true;
}
static uint32_t fake_scans(void) { return 34; }
static t5_app_api_v1 mock_app={.abi_version=1,.struct_size=sizeof(mock_app),.poll=fake_poll,.millis=fake_millis};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { (void)v; return &mock_app; }
const t5_video_api_v1 *t5_video_get_api(uint32_t v) { (void)v; return NULL; }
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v) { (void)v; return NULL; }
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY),*expected=malloc(HT_PIXELS);
    assert(memory && expected); ht_bind(memory); ht_spawn(true); app=&mock_app;
    ht_render_scene(); memcpy(expected,ht_scene,HT_PIXELS);
    memset(ht_cache_valid,0,sizeof(ht_cache_valid));
    int start=ht.x; clock_increment=2; schedule=true; ht_service=ht_render_service;
    ht_render_scene();
    assert(fake_now>110 && ht.x>start+3*256 && !(held&HT_RIGHT));
    assert(!memcmp(expected,ht_scene,HT_PIXELS));
    assert(scene_revision>1);
    /* Input can be sampled every 8ms without a sleep each time, but real
     * cooperation must still happen at 32ms, including across clock wrap. */
    clock_increment=0; schedule=false; forced_buttons=0;
    mock_app.poll_nowait=fake_nowait;
    for(unsigned wrap=0;wrap<2;++wrap) {
        uint32_t start_time=wrap?UINT32_MAX-20u:1000u;
        last_poll=last_yield=start_time; yielding_polls=nowait_polls=0;
        for(unsigned delta=8;delta<=32;delta+=8) {
            fake_now=start_time+delta; ht_render_service();
        }
        assert(nowait_polls==3 && yielding_polls==1);
        assert(last_yield==start_time+33u);
    }
    /* An old host must never expose/read the appended member. Also tolerate
     * a full-sized table with a null optional function. Both paths yield. */
    yielding_polls=nowait_polls=0; last_yield=last_poll=fake_now;
    mock_app.struct_size=offsetof(t5_app_api_v1,poll_nowait);
    fake_now+=8; ht_render_service();
    assert(yielding_polls==1 && !nowait_polls);
    mock_app.struct_size=sizeof(mock_app); mock_app.poll_nowait=NULL;
    fake_now+=8; ht_render_service();
    assert(yielding_polls==2 && !nowait_polls);
    /* Home/exit remains live even on a no-wait raster checkpoint. */
    mock_app.poll_nowait=fake_nowait; request_exit=true;
    fake_now+=8; ht_render_service(); assert(quitting && ht_abort);
    request_exit=quitting=ht_abort=false; mock_app.poll_nowait=NULL;
    /* Loading still polls input but cannot move the player or toggle pause. */
    loading=true; held=HT_RIGHT; jump_down=pause_down=true;
    start=ht.x; unsigned ticks=ht.ticks;
    ht_advance(fake_now+1000);
    assert(ht.x==start && ht.ticks==ticks && !jump_down && !pause_down);
    loading=false;
    /* Real-time pacing: 640ms at full walking speed covers 50 logical pixels,
     * half the former distance. Exercise the production accumulator. */
    ht_service=NULL; ht_spawn(true); held=HT_RIGHT; ht.vx=640;
    simulation_started=false; simulation_accumulator=0;
    paused=pause_down=jump_down=false;
    start=ht.x; ht_advance(0);
    for(uint32_t now=8;now<=640;now+=8) ht_advance(now);
    assert(ht.ticks==20 && ht.x-start>=50*256 && ht.x-start<=51*256); // Gentle spawn gust.
    /* Paused B/Up requests the AI/legacy renderer toggle without moving or
     * resuming. The rising edge is consumed by input, not by simulation. */
    paused=true; jump_down=false; mode_down=false; previous=held=0;
    start=ht.x; forced_buttons=T5_APP_BUTTON_UP;
    ht_input(1);
    assert(mode_down && paused && !jump_down && ht.x==start);
    forced_buttons=0; ht_input(1);
    /* Submission and scan rates remain separate from stage durations. */
    const t5_video_api_v1 counters={.struct_size=sizeof(counters),.frame_counter=fake_scans};
    memset(&ht_perf,0,sizeof(ht_perf));
    ht_perf.start=100;ht_perf.scan_start=10;ht_perf.frames=1;
    ht_perf.render_ms=60;ht_perf.pack_ms=20;ht_perf.wait_ms=10; ht_perf.cache_ms=16; ht_perf.copy_ms=6; ht_perf.input_ms=14;
    ht_perf.stages=(ht_render_timing){20,30,10};
    ht_perf_finish(&counters,1100);
    assert(ht_perf.fps10==20 && ht_perf.scan10==240);
    assert(ht_perf.render_avg==30 && ht_perf.pack_avg==10 && ht_perf.wait_avg==5 && ht_perf.cache_avg==8);
    assert(ht_perf.stage_avg.background==10 && ht_perf.stage_avg.world==15 && ht_perf.stage_avg.post==5);
    assert(!ht_perf.stages.background && !ht_perf.stages.world && !ht_perf.stages.post);
    assert(ht_perf.copy_avg==3 && ht_perf.copy_ms==0);
    assert(ht_perf.input_avg==7 && ht_perf.input_ms==0);
    assert(ht_perf.frames==0 && ht_perf.start==1100 && HT_FRAME_INTERVAL_MS==42);
    /* Up interacts only on its rising edge; held inputs cannot solve a
     * sequence automatically, and pause/loading do not operate machinery. */
    schedule=false; ht_service=NULL; paused=loading=false; previous=held=0;
    ht.level=8; ht_select_level(8); ht_spawn(true); ht.puzzle.stage=1;
    ht.x=(HT_PUZZLE_FIRST+HT_PUZZLE_SPACING)*256;
    ht.y=ht_land[9].top*256; ht.grounded=true;
    forced_buttons=T5_APP_BUTTON_CONFIRM; ht_input(1);
    assert(ht.puzzle.progress==1 && !jump_down && ht.vy==0);
    ht_input(1); assert(ht.puzzle.progress==1 && !ht.puzzle.wrong);
    forced_buttons=0; ht_input(1);
    forced_buttons=T5_APP_BUTTON_CONFIRM; ht_input(1);
    assert(ht.puzzle.progress==1 && ht.puzzle.wrong);
    forced_buttons=0; ht_input(1); loading=true;
    ht.x=HT_PUZZLE_FIRST*256;
    forced_buttons=T5_APP_BUTTON_CONFIRM; ht_input(1);
    assert(ht.puzzle.progress==1);
    loading=false;ht.level=0;ht_select_level(0);ht_spawn(true);
    /* Inspect, browse and close through production input. Reading freezes
     * physics and closing with A cannot also jump or activate machinery. */
    forced_buttons=0; ht_input(1);
    ht.x=ht_evidence_x(0,0)*256; ht.y=ht_surface_at(&ht,0,ht.x/256)*256; ht.grounded=true;
    forced_buttons=T5_APP_BUTTON_CONFIRM; ht_input(1);
    assert(reading && journal_page==0 && ht_evidence_found(&ht,0));
    int frozen=ht.x; unsigned frozen_ticks=ht.ticks;
    forced_buttons=T5_APP_BUTTON_RIGHT; ht_input(100);
    assert(reading && journal_page==0 && ht_journal_leaf==0 && !ht_evidence_found(&ht,1));
    ht_input(100); assert(journal_page==0 && ht.x==frozen && ht.ticks==frozen_ticks);
    forced_buttons=T5_APP_BUTTON_CONFIRM; ht_input(1);
    assert(reading && ht_journal_index && !jump_down && ht.vy==0);
    forced_buttons=T5_APP_BUTTON_BACK; ht_input(1);
    assert(!reading && !quitting);
    forced_buttons=0; ht_input(1);
    ht.x=95*256; ht.y=ht_land[0].top*256;
    forced_buttons=T5_APP_BUTTON_DOWN; ht_input(1);
    forced_buttons=0; ht_input(1);
    forced_buttons=T5_APP_BUTTON_CONFIRM; ht_input(1); assert(reading);
    forced_buttons=T5_APP_BUTTON_BACK; ht_input(1); assert(!reading && !quitting);
    ht_input(1); assert(!quitting);
    forced_buttons=0; ht_input(1);
    forced_buttons=T5_APP_BUTTON_BACK; ht_input(1); assert(quitting);
    /* Found documents and reached recaps only; preserve history on death and loop. */
    ht.evidence=1u; memset(ht.story_seen,0,sizeof(ht.story_seen)); ht.story_seen[0]=2;
    unsigned ids[HT_JOURNAL_RECORDS]; assert(ht_journal_list(ids)==2 && ids[0]==0 && ids[1]==30);
    ht_spawn(false); assert(ht.story_seen[0]==2);
    ht.level=1; ht_spawn(true); assert(ht.story_seen[0]==2 && ht.story_seen[1]==1);
    ht_journal_open(0); ht_journal_next=200; ht_journal_length=600;
    ht_journal_input(HT_RIGHT); assert(ht_journal_leaf==1 && ht_journal_offsets[1]==200);
    ht_journal_input(HT_RIGHT); assert(ht_journal_leaf==1); // Await layout before another turn.
    ht_journal_next=400; ht_journal_input(HT_RIGHT); assert(ht_journal_leaf==2);
    ht_journal_input(HT_LEFT); assert(ht_journal_leaf==1 && ht_journal_next==0);
    ht_journal_next=600; ht_journal_input(HT_RIGHT); assert(ht_journal_leaf==1); // End of entry.
    ht_journal_input(HT_EXIT); assert(ht_journal_index);
    for(unsigned i=0;i<30;++i) assert(strlen(ht_evidence_prose[i])>500 && strlen(ht_evidence_prose[i])<sizeof(ht_journal_body));
    free(expected); free(memory);
    puts("Hollow Trail: simulation advances during render; frame snapshot stays coherent PASS");
    return 0;
}

const t5_math_api_v1 *t5_math_get_api(uint32_t v) { (void)v; return NULL; }
