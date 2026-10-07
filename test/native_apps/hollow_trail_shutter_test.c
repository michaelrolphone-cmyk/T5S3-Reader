#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static void approach(void) {
    memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);
    ht.traversal.forest_log_phase=ht.traversal.bridge_open=32;
    ht.x=1074*256;ht.y=ht_land_height(0,3,1074)*256;ht.grounded=true;
    for(int n=0;n<40;++n)ht_step_controls(1,0,false,false);
    assert(ht.x==(HT_MILL_SHUTTER_LEFT-5)*256 && ht.grounded && !ht.checkpoint);
    assert(ht_traversal_near(&ht)==HT_SHUTTER);
}
static void contacts(void) {
    ht_person_pose p=ht_person_pose_at(ht.x/256-ht.camera/256,ht.y/256-ht.camera_y/256,&ht);
    for(int i=0;i<2;++i) {
        int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;
        assert(dx*dx+dy*dy<=15*15);
        dx=p.foot[i].x-p.hip.x;dy=p.foot[i].y-p.hip.y;
        if(dx*dx+dy*dy>18*18)fprintf(stderr,"phase%d leg%d length2%d\n",ht.traversal.mill_entry_phase,i,dx*dx+dy*dy);
        assert(dx*dx+dy*dy<=18*18);
        int world=p.foot[i].x+ht.camera/256,foot=p.foot[i].y+ht.camera_y/256;
        assert(foot<=ht_land_height(0,3,world));
        if(world>=HT_MILL_SHUTTER_LEFT && world<=HT_MILL_SHUTTER_RIGHT)assert(foot<=HT_MILL_SHUTTER_SILL);
    }
    assert(ht.y<=ht_land_height(0,3,ht.x/256)*256);
}
int main(void) {
    uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY);assert(mem);ht_bind(mem);ht_bind_native(mem);
    approach();int start_x=ht.x,start_y=ht.y;
    assert(ht_traversal_interact() && ht.traversal.mode==HT_SHUTTER);
    assert(ht.x==start_x && ht.y==start_y);
    for(int n=0;n<48;++n) {int x=ht.x,y=ht.y;ht_step_controls(1,0,false,false);assert(ht_abs(ht.x-x)<256 && ht_abs(ht.y-y)<256);contacts();}
    assert(ht.traversal.mill_entry_phase==48 && ht.y==HT_MILL_SHUTTER_SILL*256);
    int midx=ht.x,midy=ht.y;
    for(int n=0;n<30;++n){ht_step_controls(0,0,n==2,n==2);assert(ht.x==midx && ht.y==midy && ht.traversal.mill_entry_phase==48);}
    ht_game retained=ht;
    uint8_t *frame=malloc(HT_NATIVE_PIXELS);assert(frame);
    for(int native=0;native<2;++native) {
        ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        int size=native?HT_NATIVE_PIXELS:HT_PIXELS;
        ht_render_scene();memcpy(frame,ht_scene,size);
        ht_render_scene();assert(!memcmp(frame,ht_scene,size));
        assert(!memcmp(&ht,&retained,sizeof(ht)));
    }
    free(frame);
    assert(ht_traversal_interact() && ht.traversal.mode==HT_SHUTTER);
    for(int n=0;n<48;++n) {ht_step_controls(-1,0,false,false);contacts();}
    assert(ht.traversal.mode==HT_FREE && ht.x==start_x && ht.y==start_y && !ht.checkpoint);
    ht_step_controls(1,0,false,false);assert(ht_traversal_interact());
    for(int n=0;n<96;++n) {ht_step_controls(1,0,false,false);contacts();}
    assert(ht.traversal.mode==HT_FREE && ht.x==1111*256 && ht.grounded && ht.checkpoint==3);
    assert(!ht.evidence && !ht.scene_evidence && !ht.puzzle.solved && ht.traversal.crate_x==HT_MILL_DESK_START*256);
    assert(ht_traversal_near(&ht)==HT_CRATE && ht_traversal_interact());
    for(int n=0;n<180 && !ht_mill_register_lit(&ht);++n)ht_step_controls(1,0,false,false);
    assert(ht_mill_register_lit(&ht) && ht_inspect()==1);
    uint32_t evidence=ht.evidence;ht_spawn(false);
    assert(ht.x==1111*256 && ht.grounded && ht.evidence==evidence && ht.traversal.mode==HT_FREE);
    /* A later return uses the same opening in reverse, with no one-shot lock. */
    ht_step_controls(-1,0,false,false);assert(ht_traversal_near(&ht)==HT_SHUTTER && ht_traversal_interact());
    for(int n=0;n<96;++n)ht_step_controls(-1,0,false,false);
    assert(ht.traversal.mode==HT_FREE && ht.x==1088*256 && ht.grounded && ht.evidence==evidence);
    ht_spawn(true);assert(!ht.checkpoint && ht.traversal.mode==HT_FREE && !ht.traversal.mill_entry_phase);
    /* A partial crossing cannot survive a retry as a suspended attachment. */
    approach();assert(ht_traversal_interact());for(int n=0;n<48;++n)ht_step_controls(1,0,false,false);
    ht_spawn(false);assert(ht.x==95*256 && ht.grounded && ht.traversal.mode==HT_FREE);
    for(int side=-1;side<=1;side+=2)for(int offset=0;offset<5;++offset) {
        approach();ht.x=(side>0?1084+offset:1111+offset/2)*256;
        ht.y=ht_land_height(0,3,ht.x/256)*256;ht.facing=side;
        assert(ht_traversal_interact() && ht.traversal.mode==HT_SHUTTER);
        for(int n=0;n<96;++n) {int x=ht.x,y=ht.y;ht_step_controls(side,0,false,false);contacts();assert(ht_abs(ht.x-x)<256 && ht_abs(ht.y-y)<256);}
        assert(ht.traversal.mode==HT_FREE && ht.grounded);
    }
    free(mem);puts("Shutter: input approach, shared sill, continuous contact, pause/reverse, desk handoff, checkpoint and repeated return PASS");
}
