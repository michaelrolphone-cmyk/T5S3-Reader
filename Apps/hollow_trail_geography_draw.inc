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
    } else if(g->level==7) {
        int left=1030-cx,right=2490-cx,crest=-145-cy;
        if(right< -margin || left>HT_W+margin) return;
        ht_rect(ht_scene,left,crest,right-left,680,155);
        ht_rect(ht_scene,left-8,crest-10,right-left+16,12,195);
        for(int pier=0;pier<8;++pier) {
            int x=1060+pier*191-cx;
            ht_triangle(ht_scene,x,crest,x+28,crest,x+91,520-cy,185);
            ht_rect(ht_scene,x,crest,18,650,185);
        }
        /* Spill sheets start at the crest and disappear into basin mist. No
         * random flash/noise: a bounded slow pattern describes falling water. */
        for(int fall=0;fall<4;++fall) {
            int wx=1190+fall*352,x=wx-cx,w=fall==1?104:68;
            if(x+w< -margin || x>HT_W+margin) continue;
            ht_rect(ht_scene,x,crest+7,w,650,95);
            ht_rect(ht_scene,x+5,crest+8,w-10,650,70);
            for(int stream=0;stream<7;++stream) {
                int sx=x+8+stream*(w-16)/7;
                int start=crest+12+(int)((g->ticks*2+(unsigned)stream*31)%97);
                for(int row=0;row<6;++row) ht_rect(ht_scene,sx,start+row*97,2+stream%3,48+stream%4*8,110);
            }
            ht_rect(ht_scene,x+6,crest+8,w-12,3,45);
        }
    } else if(g->level==1) {
        /* A pair of immense near towers gives the rooftop ascent its scale. */
        for(int tower=0;tower<2;++tower) {
            int x=1710+tower*520-cx,y=-430-cy;
            if(x+190< -margin || x>HT_W+margin) continue;
            ht_rect(ht_scene,x,y,190,940,145);
            ht_rect(ht_scene,x-6,y,202,9,195);
            for(int row=0;row<16;++row) for(int col=0;col<5;++col) {
                ht_rect(ht_scene,x+14+col*34,y+20+row*44,18,28,100);
                ht_rect(ht_scene,x+21+col*34,y+20+row*44,2,28,140);
            }
        }
    }
}
