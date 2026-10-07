#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY),*expected=malloc(HT_PIXELS);assert(memory&&expected);ht_bind(memory);
    for(unsigned level=0;level<HT_LEVELS;++level)for(int x=0;x<=3200;x+=17) {
        ht.level=level;ht.x=x*256;ht.observation_ticks=0;
        assert(ht_pace_target(&ht)>=256 && ht_pace_target(&ht)<=384);
    }
    ht.level=0;ht_spawn(true);ht.grounded=true;ht.traversal.mode=HT_FREE;
    ht.vx=288;assert(!ht_character_run_blend(&ht));
    ht.vx=320;assert(ht_character_run_blend(&ht)>0 && ht_character_run_blend(&ht)<256);
    ht.vx=352;assert(ht_character_run_blend(&ht)==256);
    ht.vx=-352;assert(ht_character_run_blend(&ht)==256);
    ht.grounded=false;assert(!ht_character_run_blend(&ht));
    ht.grounded=true;ht.traversal.mode=HT_ROLL;assert(!ht_character_run_blend(&ht));
    ht.traversal.mode=HT_FREE;ht.vx=0;assert(!ht_character_run_blend(&ht));
    ht.level=0;ht_spawn(true);int close=ht.pace,start=ht.x;
    for(int n=0;n<32;++n)ht_step_controls(1,0,false,false);
    assert(ht.x-start>=30*256 && ht.x-start<=36*256); /* ~1 sec of ordinary walking. */
    for(int n=0;n<8;++n)ht_step_controls(0,0,false,false);
    assert(ht.vx==0 && ht.grounded);
    /* Y emphasis is transient: output speed/acceleration gain 10%
     * (integer-rounded) while authored pace storage remains untouched. */
    ht_spawn(true);ht.grounded=true;ht.traversal.mode=HT_FREE;
    ht.pace=300;ht_motion_emphasis=false;
    assert(ht_walk_speed(&ht)==300 && ht_motion_speed(&ht)==300);
    ht_motion_emphasis=true;
    assert(ht_motion_speed(&ht)==330 && ht.pace==300);
    assert(ht_climb_speed(&ht)==330*3/4);
    ht_motion_emphasis=false;
    ht.pace=(uint16_t)ht_pace_target(&ht);ht.vx=0;
    ht_game ordinary=ht;
    ht_step_controls(1,0,false,false);
    int ordinary_accel=ht.vx;
    ht=ordinary;ht_motion_emphasis=true;
    ht_step_controls(1,0,false,false);
    assert(ht.vx==ht_emphasis_amount(ordinary_accel));
    assert(ht.pace<=ordinary.pace+2 && ht.pace+2>=ordinary.pace);
    ht_motion_emphasis=false;
    ht.x=1840*256;assert(ht_pace_target(&ht)>close+40);
    ht_game scene=ht;scene.intimacy=ht_intimacy_target(&scene);scene.vista=ht_vista_target(&scene);
    assert(ht_scene_scale(&scene)==176);
    scene.x=95*256;scene.intimacy=ht_intimacy_target(&scene);scene.vista=ht_vista_target(&scene);
    assert(ht_scene_scale(&scene)==384); /* The same person and ground scale together. */
    ht.y=ht_surface_at(&ht,5,ht.x/256)*256;ht.vx=ht.vy=0;ht.grounded=true;
    for(int n=0;n<60;++n) {
        unsigned pace=ht.pace,proximity=ht.intimacy;
        ht_step_controls(1,0,false,false);
        assert(ht_abs((int)ht.pace-(int)pace)<=2);
        assert(ht_abs((int)ht.intimacy-(int)proximity)<=2);
    }
    /* Compare tap/hold from the same flat ground. A jump has a fixed, shorter
     * physical arc, no automatic repeat, and frozen takeoff pace. */
    ht.level=3;ht_spawn(true);ht.x=500*256;ht.y=ht_surface_at(&ht,0,500)*256;
    ht_game launch=ht;int arc[2][40];
    for(int held=0;held<2;++held) {
        ht=launch;int top=ht.y,airtime=0,takeoff=0;
        for(int n=0;n<40;++n) {
            ht_step_controls(0,0,n==0,held!=0);arc[held][n]=ht.y;if(!n)takeoff=ht.pace;
            top=ht_min(top,ht.y);if(!ht.grounded){++airtime;assert(ht.pace==takeoff);}
        }
        assert(launch.y-top>=35*256 && launch.y-top<=40*256);
        assert(airtime>=24 && airtime<=29 && ht.grounded);
    }
    assert(!memcmp(arc[0],arc[1],sizeof(arc[0])));
    /* Close forest views and wide views both obey the existing no-zoom A/B
     * switch. Camera controls cannot alter physical position or pace. */
    ht.level=0;ht_spawn(true);ht.intimacy=256;ht.vista=192;
    ht_game physical=ht;ht_camera_mode=HT_CAMERA_NO_ZOOM;ht_render_scene();memcpy(expected,ht_scene,HT_PIXELS);
    ht.intimacy=ht.vista=0;ht_render_scene();assert(!memcmp(expected,ht_scene,HT_PIXELS));
    assert(ht.x==physical.x && ht.y==physical.y && ht.pace==physical.pace);
    ht_camera_mode=HT_CAMERA_BASELINE;
    free(expected);free(memory);
    puts("Hollow Trail direction: walking pace, smooth scene cues, fixed jump arc and camera isolation PASS");
}
