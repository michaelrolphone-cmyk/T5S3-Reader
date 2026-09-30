/* Forest interactions use physical objects; scenery never supplies controls. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY),*frame=malloc(HT_PIXELS);assert(memory && frame);
    ht_bind(memory);ht.level=0;ht_spawn(true);
    int l,r,top;
    assert(!ht_mech(&ht)->plate_kind && !ht_platform_piece(&ht,1,0,&l,&r,&top));
    ht.x=366*256;ht.y=230*256;ht.vy=100;ht.grounded=false;
    ht_step_controls(1,0,false,false);assert(ht.traversal.mode!=HT_LEDGE);
    ht_spawn(true);
    ht.x=350*256;ht.y=220*256;ht.grounded=false;
    assert(!ht_forest_log_near(&ht));
    ht.grounded=true;
    for(int n=0;n<40;++n)ht_step_controls(0,0,false,false);
    assert(!ht.traversal.forest_log_phase); /* No proximity trigger. */
    assert(ht_traversal_interact() && ht.traversal.mode==HT_FREE);
    for(int n=0;n<31;++n) {
        ht_step_controls(0,0,false,false);
        assert(!ht_platform_piece(&ht,1,0,&l,&r,&top));
    }
    ht_step_controls(0,0,false,false);
    assert(ht.traversal.forest_log_phase==32 && !ht.traversal.forest_log_falling);
    assert(ht_platform_piece(&ht,1,0,&l,&r,&top) && l==374 && r==686);
    ht.x=520*256;ht.y=(ht_surface_at(&ht,1,520)-2)*256;ht.vy=600;ht.grounded=false;
    for(int n=0;n<5;++n)ht_step_controls(0,0,false,false);
    assert(ht.grounded && ht.y==ht_surface_at(&ht,1,520)*256);
    ht_spawn(false);assert(ht.traversal.forest_log_phase==32);
    ht_spawn(true);assert(!ht.traversal.forest_log_phase);
    ht.x=350*256;ht.y=220*256;assert(ht_traversal_interact());
    ht_step_controls(0,0,false,false);ht_spawn(false);
    assert(!ht.traversal.forest_log_phase && !ht.traversal.forest_log_falling);
    /* Move the original stone into the hollow with input, release it, and
     * land on its physical top. Nothing moves the banks or opens the log. */
    ht_spawn(true);ht.x=189*256;ht.y=ht_surface_at(&ht,0,189)*256;
    assert(ht_traversal_interact() && ht.traversal.mode==HT_ROLL);
    for(int n=0;n<200 && ht.traversal.ball_x<282*256;++n)ht_step_controls(1,0,false,false);
    assert(ht.traversal.ball_x>=282*256);
    if(ht.traversal.mode==HT_ROLL)assert(ht_traversal_interact());
    for(int n=0;n<60;++n)ht_step_controls(0,0,false,false);
    assert(ht.traversal.ball_y+16*256>240*256);
    assert(!ht.traversal.bridge_open && !ht.traversal.forest_log_phase);
    ht.x=ht.traversal.ball_x;ht.y=ht.traversal.ball_y-18*256;ht.vy=600;ht.grounded=false;
    for(int n=0;n<5;++n)ht_step_controls(0,0,false,false);
    assert(ht.traversal.support==1 && ht.y==ht.traversal.ball_y-16*256);
    /* Cached and fresh backgrounds agree across horizontal/vertical camera
     * motion, vista changes and returning from another chapter. */
    for(int n=0;n<12;++n) {
        ht.level=0;ht_spawn(true);ht.camera=(n*247)*256;ht.camera_y=(-n*23)*256;
        ht.vista=n%3*128;ht_render_scene();memcpy(frame,ht_scene,HT_PIXELS);
        ht_render_scene();assert(!memcmp(frame,ht_scene,HT_PIXELS));
        ht_forest_frame_valid=false;ht_render_scene();assert(!memcmp(frame,ht_scene,HT_PIXELS));
        ht_game forest=ht;ht.level=1;ht_spawn(true);ht_render_scene();
        ht=forest;ht_render_scene();assert(!memcmp(frame,ht_scene,HT_PIXELS));
    }
    free(frame);free(memory);
    puts("Hollow Trail forest: natural crossing, physical stone, respawn and backdrop reuse PASS");
}
