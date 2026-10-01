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
    /* Walk and jump into the standing snag: neither can pass through it. */
    ht.x=340*256;ht.y=220*256;ht.grounded=true;
    for(int n=0;n<120;++n)ht_step_controls(1,0,false,false);
    assert(ht.x>350*256 && ht.x<362*256 && ht.grounded && !ht.vx);
    assert(ht_forest_log_near(&ht));
    assert(ht.traversal.push_hint==HT_PUSH && ht.traversal.mode==HT_FREE);
    assert(!ht.traversal.forest_push && !ht.traversal.forest_log_falling);
    ht_step_controls(0,0,false,false);assert(!ht.traversal.push_hint);
    for(int n=0;n<80;++n) {
        ht_step_controls(1,0,n==0,n==0);
        assert(ht.x<374*256 && !ht.traversal.forest_log_falling);
    }
    /* Fast approaches from either side, including above jump height. */
    for(int y=-50;y<=220;y+=30)for(int side=-1;side<=1;side+=2) {
        int old=(374+side*80)*256;
        ht.y=y*256;ht.x=(374-side*80)*256;ht.vx=-side*1600;
        ht_forest_log_collision(old);
        assert(side<0?ht.x<374*256:ht.x>374*256);assert(!ht.vx);
    }
    ht.level=1;ht.x=380*256;ht.y=220*256;ht_forest_log_collision(340*256);
    assert(ht.x==380*256); /* No invisible snag in other chapters. */
    ht.level=0;ht_spawn(true);
    ht.x=366*256;ht.y=230*256;ht.vy=100;ht.grounded=false;
    ht_step_controls(1,0,false,false);assert(ht.traversal.mode!=HT_LEDGE);
    ht_spawn(true);
    ht.x=350*256;ht.y=220*256;ht.grounded=false;
    assert(!ht_forest_log_near(&ht));
    ht.grounded=true;
    for(int n=0;n<40;++n)ht_step_controls(0,0,false,false);
    assert(!ht.traversal.forest_log_phase); /* No proximity trigger. */
    assert(ht_traversal_interact() && ht.traversal.mode==HT_PUSH);
    for(int n=0;n<30;++n)ht_step_controls(0,0,false,false);
    assert(!ht.traversal.forest_push && !ht.traversal.forest_log_falling);
    for(int n=0;n<24;++n)ht_step_controls(1,0,false,false);
    assert(ht.traversal.forest_push==24 && !ht.traversal.forest_log_falling);
    assert(ht_forest_log_angle(&ht)>124);
    for(int n=0;n<10;++n)ht_step_controls(0,0,false,false);
    assert(!ht.traversal.forest_push && ht_forest_log_angle(&ht)==124);
    for(int n=0;n<HT_FOREST_PUSH_STEPS-1;++n) {
        ht_step_controls(1,0,false,false);
        assert(!ht.traversal.forest_log_falling && !ht_platform_piece(&ht,1,0,&l,&r,&top));
    }
    ht_step_controls(1,0,false,false);
    assert(ht.traversal.mode==HT_FREE && ht.traversal.forest_log_falling && ht.traversal.forest_push_release);
    int last_angle=ht_forest_log_angle(&ht),last_speed=0,frames=0;
    while(ht.traversal.forest_log_falling && frames<120) {
        assert(!ht_platform_piece(&ht,1,0,&l,&r,&top));
        ht_step_controls(0,0,false,false);++frames;
        assert(ht_forest_log_angle(&ht)>last_angle);
        assert(ht.traversal.forest_log_speed>last_speed);
        last_angle=ht_forest_log_angle(&ht);last_speed=ht.traversal.forest_log_speed;
    }
    assert(frames>32 && frames<90 && ht.traversal.forest_log_phase==32);
    assert(ht_platform_piece(&ht,1,0,&l,&r,&top) && l==374 && r==686);
    ht.x=520*256;ht.y=(ht_surface_at(&ht,1,520)-2)*256;ht.vy=600;ht.grounded=false;
    for(int n=0;n<5;++n)ht_step_controls(0,0,false,false);
    assert(ht.grounded && ht.y==ht_surface_at(&ht,1,520)*256);
    ht_spawn(false);assert(ht.traversal.forest_log_phase==32);
    ht_spawn(true);assert(!ht.traversal.forest_log_phase);
    ht.x=350*256;ht.y=220*256;assert(ht_traversal_interact());
    for(int n=0;n<20;++n)ht_step_controls(1,0,false,false);
    assert(ht.traversal.forest_push==20);
    ht_step_controls(-1,0,false,false);assert(ht.traversal.mode==HT_FREE && !ht.traversal.forest_log_falling);
    ht_spawn(false);
    ht.x=350*256;ht.y=220*256;assert(ht_traversal_interact());
    for(int n=0;n<12;++n)ht_step_controls(1,0,false,false);
    ht_step_controls(0,0,true,true);
    assert(ht.traversal.mode==HT_FREE && ht.vy<0 && !ht.traversal.forest_log_falling);
    ht_spawn(false);
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
    /* City shares the composed buffer: both directions of chapter changes,
     * camera moves and close/wide scales must match a forced fresh render. */
    for(int n=0;n<12;++n) {
        ht.level=1;ht_spawn(true);assert(!ht_mech(&ht)->plate_kind && ht_mech(&ht)->bridge==-1);
        ht.camera=n*239*256;ht.camera_y=(n%4*-80)*256;ht.vista=n%3*112;
        ht.intimacy=n%2?256:0;ht_render_scene();memcpy(frame,ht_scene,HT_PIXELS);
        unsigned builds=ht_cache_builds;
        ht_render_scene();assert(!memcmp(frame,ht_scene,HT_PIXELS) && builds==ht_cache_builds);
        ht_forest_frame_valid=false;ht_render_scene();assert(!memcmp(frame,ht_scene,HT_PIXELS));
        ht_game city=ht;ht.level=n%2?0:2;ht_spawn(true);ht_render_scene();
        ht=city;ht_render_scene();assert(!memcmp(frame,ht_scene,HT_PIXELS));
    }
    /* Each new chapter fully overwrites poisoned scratch and invalidates the
     * shared frame when camera, scale or chapter changes. */
    const unsigned chapters[]={2,3,5,6,7,8,9};
    for(unsigned c=0;c<sizeof(chapters)/sizeof(chapters[0]);++c)for(int n=0;n<4;++n) {
        ht.level=chapters[c];ht_spawn(true);ht.camera=n*731*256;
        ht.camera_y=-n*57*256;ht.intimacy=n%2?256:0;ht.vista=n%3*112;
        ht_render_scene();memcpy(frame,ht_scene,HT_PIXELS);unsigned builds=ht_cache_builds;
        memset(ht_scene,0x5a,HT_PIXELS);ht_render_scene();
        assert(!memcmp(frame,ht_scene,HT_PIXELS) && builds==ht_cache_builds);
        ht_forest_frame_valid=false;memset(ht_scene,0xa5,HT_PIXELS);ht_render_scene();
        assert(!memcmp(frame,ht_scene,HT_PIXELS));
        ht_game scene=ht;ht.level=4;ht_spawn(true);ht.camera=950*256;ht_render_scene();
        ht=scene;ht_render_scene();assert(!memcmp(frame,ht_scene,HT_PIXELS));
    }
    free(frame);free(memory);
    puts("Hollow Trail forest: natural crossing, physical stone, respawn and all composed chapter cache transitions PASS");
}
