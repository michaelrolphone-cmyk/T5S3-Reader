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
    /* Real-time pacing: 640ms at full walking speed covers 50 logical pixels,
     * half the former distance. Exercise the production accumulator. */
    ht_service=NULL; ht_spawn(true); held=HT_RIGHT; ht.vx=640;
    simulation_started=false; simulation_accumulator=0;
    paused=pause_down=jump_down=false;
    start=ht.x; ht_advance(0);
    for(uint32_t now=8;now<=640;now+=8) ht_advance(now);
    assert(ht.ticks==20 && ht.x-start==50*256);
    free(expected); free(memory);
    puts("Hollow Trail: simulation advances during render; frame snapshot stays coherent PASS");
    return 0;
}

const t5_math_api_v1 *t5_math_get_api(uint32_t v) { (void)v; return NULL; }
