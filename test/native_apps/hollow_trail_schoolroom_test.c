#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned checkpoints;
static void observe(void){++checkpoints;}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
    assert(memory&&copy);ht_bind(memory);ht_bind_native(memory);
    memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
    assert(ht_evidence_x(1,0)==HT_SCHOOL_MAP_X);
    ht.x=HT_SCHOOL_MAP_X*256;ht.y=ht_surface_at(&ht,1,HT_SCHOOL_MAP_X)*256;ht.grounded=true;
    assert(ht_schoolroom_near(&ht));ht_game frozen=ht;
    ht.grounded=false;assert(!ht_schoolroom_near(&ht));ht=frozen;
    ht.y+=4*256;assert(!ht_schoolroom_near(&ht));ht=frozen;
    const ht_mechanics *m=ht_mech(&ht);
    assert(ht_school_route_x(0)==m->ladders[0].x);
    assert(ht_school_route_x(1)==m->crate_x);
    assert(ht_school_route_x(2)==m->ladders[2].x);
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
        uint8_t *out=mode?ht_native_a:ht_scene_low;
        for(unsigned focus=0;focus<HT_SCHOOL_FOCUSES;++focus) {
            memset(out,0,bytes);ht_service=observe;ht_schoolroom_study_render(focus);
            assert(checkpoints>=16);assert(!memcmp(&ht,&frozen,sizeof(ht)));
            memcpy(copy,out,bytes);memset(out,255,bytes);ht_schoolroom_study_render(focus);
            assert(!memcmp(copy,out,bytes));
            ht_schoolroom_study_render(focus+1);assert(memcmp(copy,out,bytes));
            ht_schoolroom_study_render(focus+HT_SCHOOL_FOCUSES);assert(!memcmp(copy,out,bytes));
            assert(ht_native_active==(mode!=0) && !ht_native_foreground_half_y && ht_world_scale==256);
        }
    }
    ht_service=NULL;free(copy);free(memory);
    puts("Schoolroom: route-derived landmarks, real evidence floor, both rasters, all focus states, deterministic output and frozen gameplay PASS");
}
