#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/hollow_trail_cutscene.inc"
#include "../../Apps/hollow_trail_lore.inc"
static unsigned checkpoints;
static void observe(void){++checkpoints;}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
    assert(memory&&copy);ht_bind(memory);ht_bind_native(memory);
    memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
    ht.x=HT_SIGNAL_LOG_X*256;ht.y=ht_level_land(1)[7].top*256;ht.grounded=true;
    assert(ht_evidence_x(1,2)==HT_SIGNAL_LOG_X && ht_evidence_platform(1,2)==7);
    assert(ht_signal_room_near(&ht) && ht_pace_target(&ht)==272);
    ht.x=HT_SIGNAL_ROOM_LEFT*256;ht.story_x=HT_SIGNAL_ROOM_LEFT;
    ht.y=ht_level_land(1)[6].top*256;assert(ht_story_beat_at(&ht)==1);
    ht.y=ht_level_land(1)[7].top*256;assert(ht_story_beat_at(&ht)==2);
    ht.grounded=false;assert(ht_story_beat_at(&ht)==1);ht.grounded=true;
    ht.story_seen[1]=3;ht.x=1900*256;ht.y=70*256;assert(ht_story_beat_at(&ht)==2);
    ht.story_seen[1]=0;ht.x=HT_SIGNAL_LOG_X*256;ht.y=-100*256;
    assert(strstr(ht_evidence_prose[5],"Ask who attended, not whose name appears."));
    assert(strstr(ht_evidence_prose[5],"Nobody should keep the whole watch alone."));
    assert(!strstr(ht_evidence_prose[5],"copies were made below"));
    assert(!strstr(ht_evidence_prose[5],"watch dates run on"));
    ht_native_active=false;ht_scene=ht_scene_low;ht_world_scale=256;
    memset(ht_scene,0,HT_PIXELS);ht_signal_signature(100,80,0,200);
    assert(ht_scene[90*HT_W+148]==200);
    memset(ht_scene,0,HT_PIXELS);ht_signal_signature(100,80,1,200);
    assert(ht_scene[90*HT_W+148]==0);
    ht_game frozen=ht;
    ht.grounded=false;assert(!ht_signal_room_near(&ht));ht=frozen;
    ht.y+=4*256;assert(!ht_signal_room_near(&ht));ht=frozen;
    ht.x=(HT_SIGNAL_LOG_X+23)*256;assert(!ht_signal_room_near(&ht));ht=frozen;
    assert(ht_inspect()==5 && ht_evidence_found(&ht,5) && !ht.puzzle.solved);
    frozen=ht;ht_service=observe;
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
        uint8_t *out=mode?ht_native_a:ht_scene_low;
        for(unsigned focus=0;focus<HT_SIGNAL_ROOM_FOCUSES;++focus) {
            memset(out,0,bytes);ht_signal_room_study_render(focus,8);
            assert(checkpoints>=16);assert(!memcmp(&ht,&frozen,sizeof(ht)));
            memcpy(copy,out,bytes);memset(out,255,bytes);ht_signal_room_study_render(focus,8);
            assert(!memcmp(copy,out,bytes));
            ht_signal_room_study_render(focus+1,8);assert(memcmp(copy,out,bytes));
            ht_signal_room_study_render(focus+HT_SIGNAL_ROOM_FOCUSES,8);assert(!memcmp(copy,out,bytes));
            assert(ht_native_active==(mode!=0) && !ht_native_foreground_half_y && ht_world_scale==256);
        }
        ht_signal_room_study_render(0,8);memcpy(copy,out,bytes);
        ht_signal_room_study_render(0,80);assert(memcmp(copy,out,bytes));
        ht_signal_room_study_render(0,152);assert(!memcmp(copy,out,bytes));
        ht_game before=ht,after=ht;before.x=2089*256;after.x=2090*256;
        ht_cutscene_seen=0;assert(ht_cutscene_signal_arrival(&before,&after));
        after.grounded=false;assert(!ht_cutscene_signal_arrival(&before,&after));after=ht;after.x=2090*256;
        before.x=2060*256;assert(!ht_cutscene_signal_arrival(&before,&after));before.x=2089*256;
        after.y+=4*256;assert(!ht_cutscene_signal_arrival(&before,&after));after.y-=4*256;
        after.level=0;assert(!ht_cutscene_signal_arrival(&before,&after));after.level=1;
        ht=after;ht.camera=1890*256;ht.camera_y=-280*256;frozen=ht;
        ht_cutscene_begin(HT_CUTSCENE_SIGNAL);
        assert(!ht_cutscene_signal_arrival(&before,&after));
        assert(ht_signal_cues[1].end-ht_signal_cues[1].start==2*144);
        assert(ht_cutscene_track(&ht_cutscene)->duration%144u==0);
        for(unsigned tick=0;tick<576;tick+=37) {
            ht_cutscene.tick=tick;memset(out,0,bytes);ht_cutscene_render(&ht_cutscene);memcpy(copy,out,bytes);
            memset(out,255,bytes);ht_cutscene_render(&ht_cutscene);assert(!memcmp(copy,out,bytes));
            assert(!memcmp(&ht,&frozen,sizeof(ht)));
        }
        ht_cutscene.tick=575;assert(ht_cutscene_step(&ht_cutscene));ht_cutscene_apply_handoff(&ht_cutscene);
        assert(!memcmp(&ht,&frozen,sizeof(ht)) && !ht_cutscene.active && ht_cutscene.finished);
    }
    /* Both weather passes share the exact room roof, including slanted drops. */
    ht.level=1;ht.camera=1900*256;ht.camera_y=-280*256;
    assert(ht_rain_tank_weather_floor(&ht,200,202,250)==96);
    assert(ht_rain_tank_weather_floor(&ht,164,167,250)==96);
    ht.level=0;assert(ht_rain_tank_weather_floor(&ht,200,202,250)==250);
    ht_service=NULL;free(copy);free(memory);
    puts("Signal room: actual floor/log, physical comparison, animated mechanism, both rasters, earned two-cycle tableau, unchanged gameplay and roof cover PASS");
}
