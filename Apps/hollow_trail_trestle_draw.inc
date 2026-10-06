/* Recessed rail and the lower foreground route share one bounded trestle.
 * Every triangle stops at the supported edge; nothing crosses the rope gap. */
static void ht_trestle_structure(const ht_game *g) {
    int cx=g->camera/256,cy=g->camera_y/256;
    /* The surviving railway is behind the missing near-side walking strip. */
    ht_rect(ht_scene,HT_TRESTLE_LEFT-cx,207-cy,HT_TRESTLE_END-HT_TRESTLE_LEFT,4,192);
    ht_rect(ht_scene,HT_TRESTLE_LEFT-cx,214-cy,HT_TRESTLE_END-HT_TRESTLE_LEFT,3,211);
    for(int wx=HT_TRESTLE_LEFT;wx<HT_TRESTLE_END;wx+=14) {
        int length=9+(wx/14)%4;
        ht_rect(ht_scene,wx-cx,216-cy,length,6,211);
        ht_rect(ht_scene,wx-cx,216-cy,length-2,1,111);
        ht_limb(ht_scene,wx+length-3-cx,219-cy,wx+length+2-cx,222+(wx/14)%3-cy,0,0,175);
    }
    for(int k=0;k<4;++k) {
        int left=HT_TRESTLE_LEFT+8+k*64,right=ht_min(HT_TRESTLE_END-8,left+58);
        ht_limb(ht_scene,left-cx,225-cy,left+6-cx,404-cy,4,6,205);
        ht_limb(ht_scene,left-cx,226-cy,right-cx,HT_TRESTLE_FLOOR+8-cy,2,2,171);
        ht_limb(ht_scene,left+3-cx,HT_TRESTLE_FLOOR+8-cy,right-cx,227-cy,2,2,187);
        ht_limb(ht_scene,left+3-cx,HT_TRESTLE_FLOOR+8-cy,right-cx,353-cy,2,2,182);
        ht_limb(ht_scene,left+6-cx,355-cy,right-cx,HT_TRESTLE_FLOOR+8-cy,2,2,171);
        for(int b=0;b<3;++b) {
            int by=b?b==1?HT_TRESTLE_FLOOR+8:355:227;
            ht_disk(ht_scene,left+2-cx,by-cy,2,225);
            ht_rect(ht_scene,left+1-cx,by-1-cy,1,1,65);
            ht_rect(ht_scene,left+2-cx,by+3-cy,1,2,100);
        }
    }
    /* Broken wood rests on the distant ditch bank, below the usable beam. */
    ht_triangle(ht_scene,1240-cx,305-cy,1500-cx,335-cy,1500-cx,450-cy,162);
    ht_triangle(ht_scene,1240-cx,305-cy,1500-cx,450-cy,1240-cx,450-cy,162);
    for(int n=0;n<5;++n) {
        int x=1258+n*43,y=307+n*5;
        ht_limb(ht_scene,x-cx,y-cy,x+25-cx,y+11-n*5-cy,3,2,188);
        ht_limb(ht_scene,x+2-cx,y-2-cy,x+21-cx,y+8-n*5-cy,0,0,111);
    }
}
static void ht_trestle_member(const ht_game *g,int part) {
    int cx=g->camera/256,cy=g->camera_y/256;
    if(part==1) {
        ht_rect(ht_scene,HT_TRESTLE_CAP-cx,220-cy,HT_TRESTLE_END-HT_TRESTLE_CAP,18,242);
        ht_rect(ht_scene,HT_TRESTLE_CAP-cx,220-cy,HT_TRESTLE_END-HT_TRESTLE_CAP,2,148);
        return;
    }
    ht_rect(ht_scene,HT_TRESTLE_LEFT-cx,HT_TRESTLE_FLOOR-cy,HT_TRESTLE_CAP-HT_TRESTLE_LEFT,18,235);
    ht_rect(ht_scene,HT_TRESTLE_LEFT-cx,HT_TRESTLE_FLOOR-cy,HT_TRESTLE_CAP-HT_TRESTLE_LEFT,2,111);
    ht_rect(ht_scene,HT_TRESTLE_LEFT-cx,HT_TRESTLE_FLOOR+6-cy,HT_TRESTLE_CAP-HT_TRESTLE_LEFT,2,157);
    ht_rect(ht_scene,HT_TRESTLE_LEFT-cx,HT_TRESTLE_HAND-cy,HT_TRESTLE_CAP-HT_TRESTLE_LEFT,4,228);
    ht_rect(ht_scene,HT_TRESTLE_LEFT-cx,HT_TRESTLE_HAND-cy,HT_TRESTLE_CAP-HT_TRESTLE_LEFT,1,119);
    for(int side=0;side<2;++side) {
        int x=side?1474:1246;
        ht_limb(ht_scene,x-6-cx,190-cy,x-6-cx,HT_TRESTLE_FLOOR+8-cy,2,2,231);
        ht_limb(ht_scene,x+8-cx,190-cy,x+8-cx,HT_TRESTLE_FLOOR+8-cy,2,2,231);
        for(int y=220;y<=HT_TRESTLE_FLOOR;y+=6)ht_rect(ht_scene,x-4-cx,y-cy,9,2,193);
    }
    /* The wool is wound on the hand member, not collected or carried away. */
    int wx=HT_TRESTLE_WOOL-cx,wy=HT_TRESTLE_HAND-cy;
    ht_limb(ht_scene,wx-4,wy+3,wx+1,wy-6,1,0,227);
    ht_limb(ht_scene,wx-2,wy+2,wx+5,wy-4,0,0,221);
    for(int n=0;n<4;++n)ht_limb(ht_scene,wx-4+n*2,wy-2,wx-3+n*2,wy+5,1,1,n&1?68:101);
    ht_limb(ht_scene,wx+3,wy+4,wx+6,wy+12,1,0,86);
    ht_limb(ht_scene,wx+6,wy+11,wx+9,wy+13,0,0,140);
    for(int x=1256;x<1470;x+=18)if(x>=g->traversal.trestle_print_left && x<=g->traversal.trestle_print_right) {
        int age=ht_clamp(ht_abs(g->x/256-x)/4,0,70),ink=91+age;
        ht_rect(ht_scene,x-cx,HT_TRESTLE_HAND+1-cy,3,2,ink);
        for(int finger=0;finger<3;++finger)ht_rect(ht_scene,x+finger-cx,HT_TRESTLE_HAND-cy,1,1,ink);
    }
}
