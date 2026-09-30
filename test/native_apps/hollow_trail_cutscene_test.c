#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/hollow_trail_cutscene.inc"

static uint32_t image_hash(void) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<HT_PIXELS;++i){h^=ht_scene[i];h*=16777619u;}
    return h;
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY);assert(memory);ht_bind(memory);
    ht.level=0;memset(&ht,0,sizeof(ht));ht_spawn(true);
    ht_cutscene_begin(HT_CUTSCENE_INTRO);
    assert(ht_cutscene.active && ht_cutscene.tick==0 && ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_KITCHEN);
    assert(ht_cutscene_track(&ht_cutscene)->duration==HT_INTRO_TICKS);
    ht_cutscene_state frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t kitchen=image_hash();assert(kitchen);

    while(ht_cutscene.tick<90)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_KITCHEN);
    assert(ht_cutscene_intro_flash(&ht_cutscene)>0);

    while(ht_cutscene.tick<200)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_GRASS);
    frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t grass=image_hash();
    assert(grass && grass!=kitchen);

    while(ht_cutscene.tick<600)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_ORCHARD);
    int previous=ht_cutscene_actor_x(&ht_cutscene);
    for(int i=0;i<100;++i) {
        assert(!ht_cutscene_step(&ht_cutscene));
        int x=ht_cutscene_actor_x(&ht_cutscene);assert(x>=previous);previous=x;
    }
    frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t orchard=image_hash();
    assert(orchard && orchard!=grass);

    while(ht_cutscene.tick<HT_INTRO_TICKS-1)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene.active && !ht_cutscene.finished);
    assert(ht_cutscene_step(&ht_cutscene));
    assert(!ht_cutscene.active && ht_cutscene.finished && ht_cutscene.tick==HT_INTRO_TICKS);
    ht.level=7;ht_cutscene_apply_handoff(&ht_cutscene);
    assert(ht.level==0 && ht.x==95*256 && ht.grounded && ht.traversal.mode==HT_FREE);
    free(memory);
    puts("Hollow Trail cutscene: fixed timeline, flashes, autonomous actor, scene changes and forest handoff PASS");
}
