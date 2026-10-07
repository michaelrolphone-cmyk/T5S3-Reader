/* Novella II: a westward look from the signal-room terrace. This is a scenic
 * view beyond the playable roof, with no replacement contact geometry. Near
 * warehouses step down to sidings, drainage cuts and stopped pump country. */
static void ht_western_pump(int x,int y,int h,int tone) {
    int pivot=y-h+5,head=ht_max(8,h/3);
    /* Stopped pumpjack: A-frame, crank/counterweight and pitman stay behind
     * the walking beam. The curved horsehead carries a separate polish rod. */
    ht_limb(ht_scene,x-6,y,x,pivot,1,0,tone);
    ht_limb(ht_scene,x+6,y,x,pivot,1,0,tone);
    ht_limb(ht_scene,x-4,y-8,x+4,y-9,0,0,tone-10);
    ht_disk(ht_scene,x-9,y-8,4,tone);
    ht_triangle(ht_scene,x-12,y-9,x-6,y-6,x-11,y-3,tone+7);
    ht_limb(ht_scene,x-8,y-8,x-9,y-h+6,1,0,tone-5);
    ht_limb(ht_scene,x-12,y-h+7,x+12,y-h,1,2,tone);
    ht_triangle(ht_scene,x+10,y-h-1,x+15,y-h+1,x+10,y-h+head/2,tone+4);
    ht_triangle(ht_scene,x+15,y-h+1,x+18,y-h+head*2/3,x+10,y-h+head/2,tone+4);
    ht_triangle(ht_scene,x+10,y-h+head/2,x+18,y-h+head*2/3,x+16,y-h+head+2,tone+4);
    ht_limb(ht_scene,x+16,y-h+head+2,x+16,y-1,0,0,tone-12);
    ht_rect(ht_scene,x-15,y,36,2,tone-15);
}
static void ht_western_warehouse(int x,int y,int w,int h,int tone) {
    ht_rect(ht_scene,x,y,w,h,tone);
    ht_triangle(ht_scene,x-5,y,x+w*2/5,y-19,x+w+5,y,tone+9);
    ht_triangle(ht_scene,x+w*2/5,y-19,x+w+5,y,x+w*4/5,y-4,tone-16);
    ht_rect(ht_scene,x+w*3/4,y,w/4,h,tone-19);
    /* Corrugated roofing and repaired masonry stay fixed to the building. */
    for(int rib=1;rib<8;++rib) {
        int edge=x+rib*w/8,peak=x+w*2/5;
        int rise=edge<peak?(edge-x)*19/(w*2/5):(x+w-edge)*19/(w-w*2/5);
        ht_limb(ht_scene,edge,y-rise+2,edge+3,y-1,0,0,tone+17);
    }
    for(int course=0;course<h/10;++course) {
        int yy=y+8+course*10;
        ht_limb(ht_scene,x+2,yy,x+w-2,yy,0,0,tone-6);
        for(int joint=0;joint<3;++joint) {
            int xx=x+6+joint*w/3+(course%2)*9;
            if(xx<x+w-2)ht_limb(ht_scene,xx,yy,xx,yy+5,0,0,tone-7);
        }
    }
    ht_triangle(ht_scene,x+w-19,y+3,x+w-11,y+3,x+w-16,y+h*2/3,tone+7);
    ht_triangle(ht_scene,x+3,y+h-32,x+17,y+h-27,x+8,y+h,tone-14);
    ht_limb(ht_scene,x-4,y+1,x+w+4,y+1,1,1,tone+25);
    for(int bay=0;bay<4;++bay) {
        int at=x+7+bay*(w-12)/4;
        ht_rect(ht_scene,at,y+10,ht_max(6,(w-24)/4),14,tone+24);
        ht_limb(ht_scene,at,y+17,at+ht_max(6,(w-24)/4),y+17,0,0,tone-22);
        ht_limb(ht_scene,at+4,y+11,at+4,y+23,0,0,tone-12);
        if(bay==2)ht_triangle(ht_scene,at+5,y+11,at+10,y+11,at+5,y+15,tone-15);
    }
    ht_rect(ht_scene,x+w/3,y+h-37,w/4,37,tone+23);
    for(int seam=0;seam<5;++seam)ht_limb(ht_scene,x+w/3+seam*w/20,y+h-35,x+w/3+seam*w/20,y+h,0,0,tone-4);
    ht_limb(ht_scene,x+w-5,y+2,x+w-6,y+h,1,1,tone-30);
    ht_checkpoint();
}
static void ht_western_vista_draw(unsigned progress) {
    int u=ht_clamp((int)progress,0,256);
    int far=u*3/256,middle=u*7/256,near=u*14/256;
    /* Low cloud lid: continuous soft tonal layers above the empty plain. */
    for(int y=0;y<HT_H;++y) {
        int tone=ht_clamp(51-y/5+(y>124?(y-124)/6:0),23,51);
        ht_rect(ht_scene,0,y,HT_W,1,tone);
        if((y&15)==15)ht_checkpoint();
    }
    for(int band=0;band<4;++band) {
        int y=23+band*13;
        ht_triangle(ht_scene,45-far,y,265-far,y+5,490-far,y-2,44-band*3);
    }
    /* Five distinct depth planes: bluffs, flat pump ground, yard, warehouses,
     * and the close cracked cornice. No haze is placed over the close edge. */
    static const int16_t ridge[][2]={{-25,156},{33,149},{89,153},{139,146},
        {185,150},{231,143},{252,119},{311,117},{339,127},{388,129},
        {415,111},{462,110},{506,124}};
    for(unsigned i=1;i<sizeof(ridge)/sizeof(ridge[0]);++i) {
        int x0=ridge[i-1][0]-far,y0=ridge[i-1][1];
        int x1=ridge[i][0]-far,y1=ridge[i][1];
        ht_triangle(ht_scene,x0,y0,x1,y1,x0,HT_H,68);
        ht_triangle(ht_scene,x1,y1,x1,HT_H,x0,HT_H,68);
        if(i>6)ht_limb(ht_scene,x0+3,y0+6,x1-3,y1+6,0,0,53);
    }
    ht_triangle(ht_scene,154-middle,176,303-middle,149,506-middle,155,78);
    ht_rect(ht_scene,154-middle,176,360,110,78);
    ht_western_pump(294-middle,163,28,108);
    ht_western_pump(352-middle,159,36,100);
    ht_western_pump(424-middle,165,31,105);
    ht_western_pump(488-middle,170,35,110);
    ht_limb(ht_scene,197-middle,177,345-middle,169,1,1,47);
    ht_limb(ht_scene,345-middle,169,511-middle,183,1,2,47);
    ht_limb(ht_scene,197-middle,179,345-middle,171,0,0,96);
    ht_limb(ht_scene,345-middle,171,511-middle,185,0,0,96);
    /* Sidings converge into the plain; their sleepers sit within the rails. */
    ht_triangle(ht_scene,141-near,HT_H,296-middle,185,500-middle,190,99);
    ht_triangle(ht_scene,141-near,HT_H,500-middle,190,HT_W+10,HT_H,99);
    for(int rail=0;rail<3;++rail) {
        int start=147+rail*68-near,end=301+rail*17-middle;
        ht_limb(ht_scene,start,HT_H,end,187,0,0,161);
        ht_limb(ht_scene,start+9,HT_H,end+3,187,0,0,161);
        for(int sleeper=0;sleeper<7;++sleeper) {
            int t=sleeper*36,x=start+(end-start)*t/256,y=HT_H-(HT_H-187)*t/256;
            ht_limb(ht_scene,x-2,y,x+12-(sleeper/2),y,0,0,130);
        }
    }
    for(int post=0;post<12;++post) {
        int x=204+post*25-near,y=209-post;
        ht_limb(ht_scene,x,y,x,y-10,0,0,153);
        if(post<11) {
            ht_limb(ht_scene,x,y-8,x+25,y-9,0,0,130);
            ht_limb(ht_scene,x,y-4,x+25,y-5,0,0,123);
        }
    }
    /* Ground is worn ballast with a few irregular weeds at the fence line. */
    for(int patch=0;patch<16;++patch) {
        int x=218+patch*18-near,y=211+(patch%3)*8;
        ht_limb(ht_scene,x,y,x+7,y+1,0,0,106);
        if(patch%3==0) {
            ht_limb(ht_scene,x,y,x-2,y-5,0,0,127);
            ht_limb(ht_scene,x,y,x+3,y-7,0,0,119);
        }
    }
    ht_western_warehouse(142-near,186,74,57,130);
    ht_western_warehouse(55-near,171,88,88,161);
    ht_western_warehouse(-54-near,150,108,142,192);
    /* A flooded street flashes between walls, then passes beneath rooms. */
    ht_triangle(ht_scene,88-near,235,110-near,221,142-near,HT_H,47);
    ht_triangle(ht_scene,88-near,235,142-near,HT_H,73-near,HT_H,47);
    for(int ripple=0;ripple<4;++ripple)
        ht_limb(ht_scene,89-near-ripple*2,241+ripple*6,111-near+ripple*4,241+ripple*6,0,0,72);
    ht_rect(ht_scene,74-near,199,67,26,185);
    ht_limb(ht_scene,73-near,225,142-near,225,1,1,222);
    for(int window=0;window<3;++window)ht_rect(ht_scene,82-near+window*19,204,11,14,122);
    /* Nearby roof masonry ties this view to the observer's terrace. */
    ht_triangle(ht_scene,362-near,252,HT_W+20,239,HT_W+20,HT_H+20,242);
    ht_triangle(ht_scene,362-near,252,HT_W+20,HT_H+20,341-near,HT_H+20,242);
    ht_limb(ht_scene,360-near,252,HT_W+20,238,2,2,218);
    ht_limb(ht_scene,413-near,247,410-near,256,0,0,161);
    ht_limb(ht_scene,410-near,256,416-near,HT_H,0,0,161);
    for(int blade=0;blade<7;++blade) {
        int root=411-near+blade/3,y=248-blade/3;
        ht_limb(ht_scene,root,y,root-12+blade*4,y-13-blade%3*4,0,0,206);
    }
    ht_checkpoint();
}
