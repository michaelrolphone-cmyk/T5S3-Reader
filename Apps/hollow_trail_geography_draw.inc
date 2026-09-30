#include "hollow_trail_dam_draw.inc"
/* Chapter-scale geography behind the walkable foreground. Rail vehicles sit on
 * a parallel track; the reservoir wall stands behind its service stairs. These
 * are scenery, not invisible obstacles on the player's route. */
static void ht_scene_geography(const ht_game *g) {
    int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
    if(g->level==3) {
        int bed=630-cx;
        ht_rect(ht_scene,bed,219-cy,530,350,170);
        ht_triangle(ht_scene,bed-60,520-cy,bed,219-cy,bed,520-cy,170);
        ht_triangle(ht_scene,bed+530,219-cy,bed+620,520-cy,bed+530,520-cy,170);
        /* One abandoned locomotive and two carriages, not repeated icons. */
        for(int car=0;car<3;++car) {
            int wx=640+car*163,x=wx-cx,y=208-cy;
            if(x+156< -margin || x>HT_W+margin) continue;
            ht_rect(ht_scene,x-10,y+7,175,12,185); /* far-track ballast shoulder */
            ht_rect(ht_scene,x-10,y+5,175,3,225);
            for(int sleeper=0;sleeper<10;++sleeper) ht_rect(ht_scene,x-8+sleeper*18,y+8,12,3,210);
            for(int wheel=0;wheel<4;++wheel) {
                int at=x+20+wheel*33;
                ht_disk(ht_scene,at,y-3,10,225);ht_disk(ht_scene,at,y-3,6,100);
                ht_limb(ht_scene,at-5,y-3,at+5,y-3,1,1,170);
            }
            ht_rect(ht_scene,x+5,y-17,139,10,205);
            if(car==0) {
                ht_rect(ht_scene,x+20,y-50,95,33,155);
                ht_disk(ht_scene,x+20,y-34,17,155);
                ht_rect(ht_scene,x+100,y-67,43,50,175);
                ht_rect(ht_scene,x+95,y-70,53,5,205);
                ht_rect(ht_scene,x+108,y-61,26,22,65);
                ht_rect(ht_scene,x+31,y-74,13,25,210);
                ht_rect(ht_scene,x+28,y-76,19,5,225);
                ht_limb(ht_scene,x+18,y-5,x+111,y-5,2,2,130);
                ht_disk(ht_scene,x+10,y-43,5,210);
            } else {
                ht_rect(ht_scene,x+4,y-66,144,48,160);
                ht_limb(ht_scene,x+1,y-67,x+151,y-67,3,3,210);
                for(int win=0;win<6;++win) {
                    ht_rect(ht_scene,x+12+win*22,y-59,15,21,70);
                    ht_rect(ht_scene,x+18+win*22,y-59,2,21,145);
                }
                ht_rect(ht_scene,x+69,y-35,16,18,125);
                ht_limb(ht_scene,x+5,y-23,x+146,y-23,0,0,195);
            }
            ht_rect(ht_scene,x+145,y-15,20,3,210);
        }
        /* The one broken crossing is a trestle over a real cutting. */
        int x=1240-cx,y=220-cy;
        ht_rect(ht_scene,x,y+10,260,7,230);
        for(int k=0;k<4;++k) {
            int at=x+15+k*72;
            ht_limb(ht_scene,at,y+17,at+10,y+245,5,7,210);
            ht_limb(ht_scene,at+10,y+90,at+68,y+17,2,2,195);
            ht_limb(ht_scene,at+10,y+165,at+68,y+90,2,2,195);
        }
    } else if(g->level==7) ht_dam_wall(g);
}
