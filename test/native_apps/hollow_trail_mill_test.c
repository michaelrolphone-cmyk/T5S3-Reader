#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static void at_mill(int side) {
    memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);
    ht.traversal.forest_log_phase=ht.traversal.bridge_open=32;
    ht.x=(HT_MILL_DESK_START+side*17)*256;ht.y=ht_land_height(0,3,ht.x/256)*256;
    ht.grounded=true;ht.camera=(HT_MILL_DESK_START-200)*256;ht.camera_y=(ht.y/256-180)*256;
}
int main(void) {
    uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);assert(mem&&copy);
    ht_bind(mem);ht_bind_native(mem);
    for(int side=-1;side<=1;side+=2) {
        at_mill(side);assert(ht.traversal.crate_y/256==ht_land_height(0,3,HT_MILL_DESK_START));
        assert(!ht_mill_register_lit(&ht) && ht_inspect()==-1 && !ht_evidence_found(&ht,1));
        assert(ht_traversal_near(&ht)==HT_CRATE && ht_traversal_interact());
        unsigned steps=0;
        while(!ht_mill_register_lit(&ht) && steps++<180)ht_step_controls(1,0,false,false);
        assert(steps<180 && ht.traversal.mode==HT_CRATE && ht.grounded);
        assert(ht.traversal.crate_x>HT_MILL_DESK_START*256);
        assert(ht_abs(ht.y/256-ht_land_height(0,3,ht.x/256))<=2);
        assert(ht_evidence_world_x(&ht,1)==ht.traversal.crate_x/256);
        assert(ht_inspect()==1 && ht_evidence_found(&ht,1));
        assert(ht.traversal.mode==HT_FREE && !ht.traversal.crate_vx && !ht.vx);
        uint32_t evidence=ht.evidence;assert(ht_inspect()==1 && ht.evidence==evidence);
        ht_game retained=ht;
        ht.traversal.crate_y-=10*256;assert(!ht_mill_register_lit(&ht));ht=retained;
        ht.traversal.crate_x=1200*256;assert(!ht_mill_register_lit(&ht));ht=retained;
        for(unsigned mode=0;mode<2;++mode) {
            ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
            size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
            uint8_t *out=mode?ht_native_a:ht_scene_low;
            memset(out,0,bytes);ht_render_scene();memcpy(copy,out,bytes);
            memset(out,255,bytes);ht_render_scene();assert(!memcmp(copy,out,bytes));
            assert(!memcmp(&ht,&retained,sizeof(ht)));
        }
        int left=ht.traversal.mill_scrape_left,right=ht.traversal.mill_scrape_right;
        assert(left==HT_MILL_DESK_START+9 && right>left);
        ht_spawn(false);assert(ht_evidence_found(&ht,1));
        assert(ht.traversal.mill_scrape_left==left && ht.traversal.mill_scrape_right==right);
    }
    /* Body travel stops at the existing room walls and stays on real ground. */
    at_mill(1);ht.traversal.crate_x=1199*256;ht.traversal.crate_y=ht_land_height(0,3,1199)*256;
    ht.x=(1199+17)*256;ht.y=ht_land_height(0,3,ht.x/256)*256;ht.traversal.mode=HT_CRATE;
    for(unsigned i=0;i<40;++i)ht_step_controls(1,0,false,false);
    assert(ht.traversal.crate_x==1200*256 && !ht.traversal.crate_vx);
    int farthest=ht.traversal.mill_scrape_right;
    for(unsigned i=0;i<70;++i)ht_step_controls(-1,0,false,false);
    assert(ht.traversal.crate_x<1200*256 && ht.traversal.mill_scrape_right==farthest);
    free(copy);free(mem);puts("Mill: real push/pull into window light, dynamic evidence, grounded legs/body, read release, repeat/archive and bounded travel PASS");
}
