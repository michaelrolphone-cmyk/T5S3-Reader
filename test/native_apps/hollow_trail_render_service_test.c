/* The production render checkpoints must advance gameplay while preserving
 * one coherent displayed snapshot. No controller setup/mapping changes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
static uint32_t fake_now,clock_increment;
static bool schedule;
static uint32_t fake_millis(void) { fake_now+=clock_increment; return fake_now; }
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    fake_now+=wait;
    *out=(t5_app_input_t){.buttons=schedule && fake_now>=20 && fake_now<110?T5_APP_BUTTON_RIGHT:0};
    return true;
}
static uint32_t fake_scans(void) { return 34; }
static const t5_app_api_v1 mock_app={.abi_version=1,.struct_size=sizeof(mock_app),.poll=fake_poll,.millis=fake_millis};
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
    assert(ht.ticks==20 && ht.x-start==50*256);
    /* Paused confirm requests DSP toggle, without moving or resuming. */
    paused=true; jump_down=true; mode_down=false;
    start=ht.x; ht_advance(648);
    assert(mode_down && paused && !jump_down && ht.x==start);
    /* Submission and scan rates remain separate from stage durations. */
    const t5_video_api_v1 counters={.struct_size=sizeof(counters),.frame_counter=fake_scans};
    memset(&ht_perf,0,sizeof(ht_perf));
    ht_perf.start=100;ht_perf.scan_start=10;ht_perf.frames=1;
    ht_perf.render_ms=60;ht_perf.pack_ms=20;ht_perf.wait_ms=10; ht_perf.cache_ms=16; ht_perf.copy_ms=6;
    ht_perf_finish(&counters,1100);
    assert(ht_perf.fps10==20 && ht_perf.scan10==240);
    assert(ht_perf.render_avg==30 && ht_perf.pack_avg==10 && ht_perf.wait_avg==5 && ht_perf.cache_avg==8);
    assert(ht_perf.copy_avg==3 && ht_perf.copy_ms==0);
    assert(ht_perf.frames==0 && ht_perf.start==1100 && HT_FRAME_INTERVAL_MS==42);
    free(expected); free(memory);
    puts("Hollow Trail: simulation advances during render; frame snapshot stays coherent PASS");
    return 0;
}

const t5_math_api_v1 *t5_math_get_api(uint32_t v) { (void)v; return NULL; }
