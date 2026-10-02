#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static double screen_y(const ht_game *g,unsigned mode,int x,int y) {
    int turn=0,scale=4096;
    if(mode!=HT_CAMERA_OFF && (g->sway_phase || g->drop_zoom)) {
        ht_camera_coefficients(g,&turn,&scale);
        if(mode==HT_CAMERA_NO_ROTATION) {
            turn=0;scale=4096-(ht_sway_wave(g->sway_phase/2+768)+256)*4/3;
            scale+=(4096-scale)*g->drop_zoom/256;
        } else if(mode==HT_CAMERA_NO_ZOOM)scale=4096;
    }
    double px=(x-g->camera/256-240)*ht_scene_scale(g)/256.0;
    double py=(y-g->camera_y/256-135)*ht_scene_scale(g)/256.0;
    return 135+4096*(px*turn+py*scale)/((double)scale*scale+(double)turn*turn);
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY);assert(memory);ht_bind(memory);unsigned cases=0;
    for(unsigned level=0;level<HT_LEVELS;++level)for(unsigned mode=0;mode<4;++mode)
        for(int phase=0;phase<1024;phase+=31) {
            ht.level=level;ht_spawn(true);ht.x=1430*256;ht.y=150*256;
            ht.camera=(1430-200)*256;ht.camera_y=(150-260)*256;
            ht.intimacy=256;ht.vista=(unsigned)phase%257;ht.sway_phase=phase;ht.rotation_phase=(phase*3)<<8;ht.camera_mood=256;
            if(ht_mech(&ht)->boat_right){ht.traversal.mode=HT_BOAT;ht.y=ht_boat_top(&ht);}
            ht_game g=ht,original=ht;
            if(mode==HT_CAMERA_NO_ZOOM || mode==HT_CAMERA_OFF)g.intimacy=g.vista=0;
            ht_frame_camera(&g,mode);
            int half=g.traversal.mode==HT_BOAT?ht_boat_half(&g)+16:24;
            int bottom=g.traversal.mode==HT_BOAT?ht_boat_top(&g)/256+29:g.y/256+26;
            for(int side=-1;side<=1;side+=2)assert(screen_y(&g,mode,g.x/256+side*half,bottom)<=214);
            assert(!memcmp(&ht,&original,sizeof(ht)));++cases;
        }
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level;ht_spawn(true);const ht_mechanics *m=ht_mech(&ht);if(!m->rope_length)continue;
        ht.traversal.mode=HT_ROPE;ht.traversal.rope_grip=50*256;
        ht.x=(m->rope_anchor_x+40)*256;int deepest=0;
        for(int n=0;n<80;++n) {
            int oldx=ht.traversal.rope_bend_x,oldy=ht.traversal.rope_bend_y;
            ht_props_step(1);ht_traversal *t=&ht.traversal;
            assert(ht_abs(t->rope_bend_x-oldx)<=96 && ht_abs(t->rope_bend_y-oldy)<=96);
            assert(t->rope_px[0]==m->rope_anchor_x*256+t->rope_bend_x);
            assert(t->rope_py[0]==m->rope_anchor_y*256+t->rope_bend_y);
            deepest=ht_max(deepest,t->rope_bend_y);
        }
        assert(deepest>=256 && deepest<=5*256);
        if(level==0) {
            ht_climb_tree tree;assert(ht_existing_tree(&ht,HT_PLATFORMS,&tree));
            ht_tree_branch b=ht_rope_branch(&ht,tree.x);int top;
            assert(ht_tree_footing(&ht,&tree,5,b.mx,&top) && top==b.my-b.mr);
        }
        ht.traversal.mode=HT_FREE;
        for(int n=0;n<240;++n)ht_props_step(0);
        assert(!ht.traversal.rope_bend_x && !ht.traversal.rope_bend_y);
    }
    free(memory);printf("Caption-safe framing: %u camera cases; loaded rope pin, damping, return and limb contact PASS\n",cases);
}
