#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/hollow_trail_cutscene.inc"
int main(void) {
    uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
    assert(mem&&copy);ht_bind(mem);ht_bind_native(mem);
    for(unsigned native=0;native<2;++native) {
        ht_native_active=native!=0;ht_native_foreground_half_y=false;
        ht_scene=native?ht_native_a:ht_scene_low;ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        ht_world_scale=256;size_t bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
        for(unsigned level=0;level<HT_LEVELS;++level) {
            memset(&ht,0,sizeof(ht));ht.level=level;ht_select_level(level);ht_spawn(true);
            ht.camera=2955*256;ht.camera_y=20*256;
            const ht_platform *p=&ht_level_land(level)[9];
            memset(ht_scene,0,bytes);ht_draw_land(&ht,9,0,p->left,p->right,p->top);memcpy(copy,ht_scene,bytes);
            ht.puzzle.solved=true;ht_game retained=ht;
            memset(ht_scene,0,bytes);ht_draw_land(&ht,9,0,p->left,p->right,p->top);
            assert(!memcmp(&ht,&retained,sizeof(ht)));
            if(level==1)assert(memcmp(copy,ht_scene,bytes));else assert(!memcmp(copy,ht_scene,bytes));
        }
        memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
        ht.x=(HT_PUZZLE_FIRST+3*HT_PUZZLE_SPACING)*256;ht.y=200*256;ht.grounded=true;
        ht.camera=2955*256;ht.camera_y=20*256;ht.intimacy=256;ht_cutscene_seen=0;
        ht_game before=ht;ht.puzzle.solved=true;
        assert(ht_cutscene_relay_restored(&before,&ht));
        ht.y=70*256;assert(!ht_cutscene_relay_restored(&before,&ht));ht.y=200*256;
        ht.grounded=false;assert(!ht_cutscene_relay_restored(&before,&ht));ht.grounded=true;
        ht_game retained=ht;ht_cutscene_begin(HT_CUTSCENE_RELAY);
        assert(!ht_cutscene_relay_restored(&before,&ht));
        for(unsigned tick=0;tick<=432;tick+=36) {
            ht_cutscene.tick=tick;ht_game view=ht_cutscene_arrival_view(&ht_cutscene);
            if(tick==0 || tick==432) {
                assert(view.camera==retained.camera && view.camera_y==retained.camera_y);
                assert(view.intimacy==retained.intimacy && view.vista==retained.vista);
            }
            memset(ht_scene,0,bytes);ht_cutscene_render(&ht_cutscene);memcpy(copy,ht_scene,bytes);
            memset(ht_scene,255,bytes);ht_cutscene_render(&ht_cutscene);assert(!memcmp(copy,ht_scene,bytes));
            assert(!memcmp(&ht,&retained,sizeof(ht)));
        }
        ht_cutscene.tick=431;assert(ht_cutscene_step(&ht_cutscene));ht_cutscene_apply_handoff(&ht_cutscene);
        assert(!memcmp(&ht,&retained,sizeof(ht)));
    }
    free(copy);free(mem);puts("City relay: solved-state lower windows only, player-earned cue, both rasters, frozen state and exact framing return PASS");
}
