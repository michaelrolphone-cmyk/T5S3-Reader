#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/hollow_trail_cutscene.inc"

static unsigned ink_rect(int x,int y,int w,int h) {
    unsigned ink=0;int scale=ht_native_active?2:1,width=HT_W*scale;
    for(int yy=y*scale;yy<(y+h)*scale;++yy)for(int xx=x*scale;xx<(x+w)*scale;++xx)
        ink+=ht_scene[yy*width+xx]>225;
    return ink;
}
static uint32_t region_hash(int x,int y,int w,int h) {
    uint32_t hash=2166136261u;int scale=ht_native_active?2:1,width=HT_W*scale;
    for(int yy=y*scale;yy<(y+h)*scale;++yy)for(int xx=x*scale;xx<(x+w)*scale;++xx)
        {hash^=ht_scene[yy*width+xx];hash*=16777619u;}
    return hash;
}
static void frame(unsigned tick) {ht_cutscene.tick=(uint16_t)tick;ht_cutscene_render(&ht_cutscene);}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*prior=malloc(HT_NATIVE_PIXELS);
    assert(memory && prior);ht_bind(memory);ht_bind_native(memory);
    memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);
    ht.x=(HT_MILL_DESK_START-17)*256;ht.y=ht_land_height(0,3,ht.x/256)*256;ht.grounded=true;
    assert(!ht_cutscene_register_inspected(ht_inspect(),&ht));
    assert(ht_traversal_interact());unsigned steps=0;
    while(!ht_mill_register_lit(&ht) && steps++<180)ht_step_controls(1,0,false,false);
    assert(steps<180 && ht_inspect()==1 && ht_cutscene_register_inspected(1,&ht));
    ht_game earned=ht;
    ht.grounded=false;assert(!ht_cutscene_register_inspected(1,&ht));ht=earned;
    ht.x-=50*256;assert(!ht_cutscene_register_inspected(1,&ht));ht=earned;
    ht.traversal.crate_y-=20*256;assert(!ht_cutscene_register_inspected(1,&ht));ht=earned;
    ht.traversal.crate_x=HT_MILL_DESK_START*256;assert(!ht_cutscene_register_inspected(1,&ht));ht=earned;
    ht.evidence=0;assert(!ht_cutscene_register_inspected(1,&ht));ht=earned;
    assert(!ht_cutscene_register_inspected(0,&ht));
    ht_cutscene_begin(HT_CUTSCENE_REGISTER);
    assert(ht_cutscene_track(&ht_cutscene)->duration==HT_REGISTER_MEMORY_TICKS);
    for(unsigned i=0;i<7;++i)assert(ht_register_cues[i].end-ht_register_cues[i].start>=160);
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
        uint8_t *out=mode?ht_native_a:ht_scene_low;
        for(unsigned tick=0;tick<HT_REGISTER_MEMORY_TICKS;tick+=37) {
            memset(out,0,bytes);frame(tick);memcpy(prior,out,bytes);
            memset(out,255,bytes);frame(tick);assert(!memcmp(prior,out,bytes));
            assert(!memcmp(&ht,&earned,sizeof(ht)));
        }
        frame(250);uint32_t writing=region_hash(195,78,55,80);
        frame(355);assert(writing!=region_hash(195,78,55,80)); /* wrist/hair actually move */
        frame(530);uint32_t bread=region_hash(270,120,62,60);
        frame(675);assert(bread!=region_hash(270,120,62,60)); /* loaf enters bag */
        frame(1240);unsigned open=ink_rect(145,78,100,80);uint32_t her_name=region_hash(144,58,104,12);
        memcpy(prior,out,bytes);
        frame(1070);assert(ink_rect(145,78,100,80)>open+2500u*(mode?4u:1u));
        assert(her_name==region_hash(144,58,104,12)); /* her actual name stays exposed */
        /* Count real handwritten strokes hidden by the visible hand on EACH
         * lower row. Aggregate palm area could miss the uppermost lower name.
         * The rule beneath her row is outside the protected name rectangle. */
        int raster=mode?2:1,width=HT_W*raster;
        for(int row=1;row<7;++row) {
            unsigned letters=0,covered=0;int y=65+row*13;
            for(int yy=(y-6)*raster;yy<=(y+3)*raster;++yy)
                for(int xx=147*raster;xx<240*raster;++xx) {
                    uint8_t ink=prior[yy*width+xx];
                    if(ink>=170 && ink<=190) {++letters;covered+=out[yy*width+xx]>225;}
                }
            assert(letters && covered*4>=letters);
        }
        frame(930);uint32_t shake=region_hash(92,34,35,182);
        frame(935);assert(shake!=region_hash(92,34,35,182)); /* paper against fixed thumb */
        frame(1240);uint32_t blank=region_hash(130,192,238,21);
        frame(1360);assert(blank!=region_hash(130,192,238,21)); /* final writing */
    }
    ht_cutscene.tick=HT_REGISTER_MEMORY_TICKS-1;
    assert(ht_cutscene_step(&ht_cutscene));ht_cutscene_apply_handoff(&ht_cutscene);
    assert(!memcmp(&ht,&earned,sizeof(ht)));
    /* Reinspection is an explicit replay, with no repeated reward or reset. */
    assert(ht_inspect()==1 && !memcmp(&ht,&earned,sizeof(ht)));
    ht_cutscene_begin(HT_CUTSCENE_REGISTER);assert(ht_cutscene.active && ht_cutscene.tick==0);
    assert(!memcmp(&ht_cutscene_anchor,&earned,sizeof(ht)));
    /* Fixed interior vertical at every old camera phase; smooth approach and
     * the original outdoor/chapter camera remain selectable. */
    for(unsigned phase=0;phase<1024;phase+=17) {
        ht.sway_phase=phase*2;ht.rotation_phase=phase*256;ht.camera_mood=256;
        int turn,scale;ht_camera_coefficients(&ht,&turn,&scale);assert(!turn && scale==4096);
    }
    int previous_gain=256;
    for(int x=1080;x<=1120;++x) {
        ht.x=x*256;ht.y=ht_land_height(0,3,x)*256;
        int gain=ht_camera_motion_gain(&ht);assert(gain<=previous_gain && previous_gain-gain<=7);previous_gain=gain;
    }
    assert(!previous_gain);ht.level=1;assert(ht_camera_motion_gain(&ht)==256);
    ht=earned;ht.y-=20*256;assert(ht_camera_motion_gain(&ht)==256);
    free(prior);free(memory);
    puts("Mill memory: physical eligibility, seven deliberate beats, real gestures, six-name cover, both rasters, state/replay and quiet interior camera PASS");
}
