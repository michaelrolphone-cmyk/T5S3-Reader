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
static void render_face(int face,uint32_t *hash) {
    ht_cutscene_camera camera={HT_CUT_VIEW_FORWARD_X,0,0,48,256,420,138,6};
    ht_cutscene_fill(6);ht_cutscene_objects_reset();
    ht_cutscene_queue_actor(220,0,0,HT_CUT_POSE_IDLE,face,7);
    ht_cutscene_render_objects(&camera);*hash=image_hash();
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY);assert(memory);ht_bind(memory);
    ht.level=0;memset(&ht,0,sizeof(ht));ht_spawn(true);
    ht_cutscene_begin(HT_CUTSCENE_INTRO);
    assert(ht_cutscene.active && ht_cutscene.tick==0);
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_KITCHEN);
    assert(ht_cutscene_cue_at(&ht_cutscene)->view==HT_CUT_VIEW_FORWARD_X);
    assert(ht_cutscene_track(&ht_cutscene)->duration==HT_INTRO_TICKS);

    ht_cutscene_camera camera=ht_cutscene_camera_at(&ht_cutscene);
    ht_cutscene_projection near=ht_cutscene_project_forward(&camera,(ht_cut_vec3){100,80,0});
    ht_cutscene_projection far=ht_cutscene_project_forward(&camera,(ht_cut_vec3){800,80,0});
    assert(near.scale>far.scale);
    assert(ht_abs(near.x-240)>ht_abs(far.x-240));
    assert(ht_cutscene_depth_tone(&camera,near.depth,220)>ht_cutscene_depth_tone(&camera,far.depth,220));

    ht_cutscene_state frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t kitchen=image_hash();assert(kitchen);

    while(ht_cutscene.tick<105)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_KITCHEN);
    assert(ht_cutscene_intro_flash(&ht_cutscene)>0);

    while(ht_cutscene.tick<220)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_GRASS);
    assert(ht_cutscene_cue_at(&ht_cutscene)->view==HT_CUT_VIEW_FORWARD_X);
    frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t grass=image_hash();
    assert(grass && grass!=kitchen);

    uint32_t front,back,quarter;render_face(HT_CUT_FACE_FRONT,&front);
    render_face(HT_CUT_FACE_BACK,&back);render_face(HT_CUT_FACE_RIGHT_3Q,&quarter);
    assert(front!=back && back!=quarter && front!=quarter);

    while(ht_cutscene.tick<620)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_DEPARTURE);
    assert(ht_cutscene_cue_at(&ht_cutscene)->view==HT_CUT_VIEW_FORWARD_X);
    int previous=ht_cutscene_actor_x(&ht_cutscene);
    for(int i=0;i<80;++i) {
        assert(!ht_cutscene_step(&ht_cutscene));
        int x=ht_cutscene_actor_x(&ht_cutscene);assert(x>=previous);previous=x;
    }
    frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t departure=image_hash();
    assert(departure && departure!=grass);

    while(ht_cutscene.tick<760)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_ORCHARD);
    assert(ht_cutscene_cue_at(&ht_cutscene)->view==HT_CUT_VIEW_PROFILE);
    frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t orchard=image_hash();
    assert(orchard && orchard!=departure);

    while(ht_cutscene.tick<HT_INTRO_TICKS-1)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene.active && !ht_cutscene.finished);
    assert(ht_cutscene_cue_at(&ht_cutscene)->framing_to==384);
    assert(ht_cutscene_step(&ht_cutscene));
    assert(!ht_cutscene.active && ht_cutscene.finished && ht_cutscene.tick==HT_INTRO_TICKS);
    ht.level=7;ht_cutscene_apply_handoff(&ht_cutscene);
    assert(ht.level==0 && ht.x==95*256 && ht.grounded && ht.traversal.mode==HT_FREE);
    assert(ht_scene_scale(&ht)==384);
    free(memory);
    puts("Hollow Trail cutscene: Forward-X depth projection, depth sorting, character views, profile transition and forest handoff PASS");
}
