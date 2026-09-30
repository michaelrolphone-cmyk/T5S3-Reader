/* Dam detail stays behind the playable bank. Fixed geometry and advected
 * ribbons describe volume without random per-frame noise or extra buffers. */
static int ht_dam_camera(const ht_game *g) {
    return 1300+(g->camera/256-1300)*88/100;
}
static void ht_dam_sheet(const ht_game *g,int wx,int width,int basin) {
    int cx=ht_dam_camera(g),cy=g->camera_y/256,x=wx-cx,top=-138-cy;
    if(x+width+55< -ht_view_margin() || x-55>HT_W+ht_view_margin())return;
    int height=basin+138;
    /* Recess, wet shoulders and shadow under the projecting spill lip. */
    ht_rect(ht_scene,x-9,top-10,width+18,height+18,177);
    ht_rect(ht_scene,x-4,top,width+8,height,117);
    int pl=x,pr=x+width,py=top;
    for(int i=1;i<=32;++i) {
        int y=top+height*i/32;
        unsigned h=ht_hash((unsigned)(wx+i*73));
        int spread=i*i/110,left=x-spread+(int)(h%5)-2,right=x+width+spread+(int)((h>>5)%5)-2;
        int shade=83-i/2;
        ht_triangle(ht_scene,pl,py,pr,py,left,y,shade);
        ht_triangle(ht_scene,pr,py,right,y,left,y,shade);
        /* The central body is luminous; torn side veils retain wall shadow. */
        ht_triangle(ht_scene,pl+6,py,pr-9,py,left+7,y,shade-19);
        ht_triangle(ht_scene,pr-9,py,right-8,y,left+7,y,shade-19);
        ht_limb(ht_scene,pl,py,left,y,1,1,106-i);
        ht_limb(ht_scene,pr-2,py,right-2,y,1,1,110-i);
        pl=left;pr=right;py=y;
    }
    /* Broad folded ribbons keep continuous flow between bright packets.
     * Unequal paths and widths describe overlapping sheets, not ruled lines. */
    for(int ribbon=0;ribbon<6;++ribbon) {
        int base=x+7+ribbon*(width-14)/6,px=base,py=top;
        for(int k=1;k<=16;++k) {
            unsigned h=ht_hash((unsigned)(wx+ribbon*131+k*17));
            int nx=base+(int)(h%7)-3+(ribbon-3)*k*k/180,ny=top+height*k/16;
            ht_limb(ht_scene,px,py,nx,ny,ribbon%2?2:1,ribbon%2?2:1,ribbon%2?81-k:44);
            px=nx;py=ny;
        }
    }
    for(int stream=0;stream<12;++stream) {
        int sx=x+3+stream*(width-6)/12;
        unsigned seed=ht_hash((unsigned)(wx+stream*97));
        /* Equal-time packets stretch as their fall accelerates. */
        for(int packet=0;packet<4;++packet) {
            int age=(int)((g->ticks+seed+packet*32u)%128u);
            int y=height*age*age/16384,tail=height*ht_max(0,age-9)*ht_max(0,age-9)/16384;
            int drift=(int)(seed%7)-3;
            int ex=sx+drift*age/128;
            ht_limb(ht_scene,sx+drift*ht_max(0,age-9)/128,top+tail,ex,top+y,
                stream%3==0?1:0,0,stream%3==0?105:39);
        }
    }
    ht_limb(ht_scene,x-6,top-2,x+width+6,top-2,3,2,197);
    ht_limb(ht_scene,x+1,top+2,x+width-1,top+2,1,1,32);
}
static void ht_dam_wall(const ht_game *g) {
    int cx=ht_dam_camera(g),cy=g->camera_y/256,left=1030-cx,right=2490-cx,crest=-145-cy;
    if(right< -ht_view_margin() || left>HT_W+ht_view_margin())return;
    ht_rect(ht_scene,left,crest,right-left,680,155);
    /* Broad courses, runoff stains and hairline fractures belong to concrete,
     * not a checkerboard of platforms. Detail remains anchored to the wall. */
    for(int bay=0;bay<8;++bay) {
        int x=1060+bay*191-cx;
        if(x+191< -ht_view_margin() || x>HT_W+ht_view_margin())continue;
        ht_triangle(ht_scene,x+18,crest,x+91,520-cy,x+113,520-cy,119);
        ht_triangle(ht_scene,x,crest,x+28,crest,x+91,520-cy,188);
        ht_triangle(ht_scene,x,crest,x,520-cy,x+91,520-cy,176);
        ht_limb(ht_scene,x+22,crest+10,x+86,490-cy,1,1,137);
        for(int row=0;row<12;++row) {
            int y=crest+22+row*51;
            ht_limb(ht_scene,x+34,y,x+178,y-2,0,0,142);
            int crack=x+45+(int)(ht_hash((unsigned)(bay*71+row))%105);
            ht_limb(ht_scene,crack,y,crack-3,y+12,0,0,165);
            ht_limb(ht_scene,crack-3,y+12,crack+2,y+22,0,0,162);
        }
    }
    for(int fall=0;fall<4;++fall) {
        int wx=1190+fall*352;
        /* The broad central discharge meets the boat basin; outer chutes
         * continue into the lower gorge, behind the solid service banks. */
        ht_dam_sheet(g,wx,fall==1?104:68,fall==1?160:340);
    }
    ht_rect(ht_scene,left-8,crest-10,right-left+16,12,195);
    ht_limb(ht_scene,left-8,crest-11,right+8,crest-11,1,1,125);
    for(int rail=1030;rail<=2490;rail+=39) {
        int x=rail-cx;if(x< -ht_view_margin() || x>HT_W+ht_view_margin())continue;
        ht_limb(ht_scene,x,crest-12,x,crest-26,1,1,171);
    }
    ht_limb(ht_scene,left,crest-25,right,crest-25,1,1,168);
}
/* Feathered mist blends with the actual wall/water beneath it. Two-pixel
 * coverage is bounded and clipped; nearer terrain and the boat draw afterward. */
static void ht_dam_mist(int wx,int wy,int rx,int ry,int opacity,const ht_game *g) {
    int cx=ht_project_x(wx-ht_dam_camera(g)),cy=ht_project_y(wy-g->camera_y/256);
    rx=ht_max(1,rx*ht_world_scale/256);ry=ht_max(1,ry*ht_world_scale/256);
    int l=ht_max(0,cx-rx)&~1,r=ht_min(HT_W,cx+rx),top=ht_max(0,cy-ry)&~1,bottom=ht_min(HT_H,cy+ry);
    for(int y=top;y<bottom;y+=2)for(int x=l;x<r;x+=2) {
        int dx=(x-cx)*256/rx,dy=(y-cy)*256/ry;
        int weight=ht_max(0,256-(dx*dx+dy*dy)/256);
        int a=weight*weight/256*opacity/256;
        if(!a)continue;
        for(int yy=y;yy<ht_min(y+2,HT_H);++yy)for(int xx=x;xx<ht_min(x+2,HT_W);++xx) {
            uint8_t *p=ht_scene+yy*HT_W+xx;*p=(uint8_t)((*p*(256-a)+42*a)/256);
        }
    }
}
static void ht_dam_spray(const ht_game *g) {
    if(g->level!=7)return;
    int cx=ht_dam_camera(g),cy=g->camera_y/256;
    for(int fall=0;fall<4;++fall) {
        int wx=1190+fall*352,w=fall==1?104:68,basin=fall==1?160:340;
        if(wx+w+95-cx< -ht_view_margin() || wx-95-cx>HT_W+ht_view_margin())continue;
        int phase=(int)((g->ticks/3+(unsigned)fall*17)%64),drift=(phase<32?phase:64-phase)/4-4;
        ht_dam_mist(wx+w/2,basin-4,83,21,160,g);
        ht_dam_mist(wx+w/2+drift,basin-27,62,32,104,g);
        ht_dam_mist(wx+w/2-drift*2,basin-55,39,39,48,g);
        for(int ring=0;ring<5;++ring) {
            int age=(int)((g->ticks/2+(unsigned)ring*13)%64),reach=15+age;
            int y=basin+3+age/5,center=wx+w/2;
            int l=center-reach,r=center+reach;
            if(fall==1){l=ht_max(l,1261);r=ht_min(r,1619);}
            ht_limb(ht_scene,l-cx,y-cy,ht_min(r,l+reach/2)-cx,y-1-cy,0,0,58+age);
            ht_limb(ht_scene,ht_max(l,r-reach/3)-cx,y-cy,r-cx,y+1-cy,0,0,58+age);
        }
    }
}
static void ht_dam_foreground(const ht_game *g) {
    int cx=g->camera/256*5/4,cy=g->camera_y/256*5/4;
    for(int n=0;n<2;++n) {
        int x=(n?2980:1100)-cx,y=230-cy;
        if(x+125< -ht_view_margin() || x-90>HT_W+ht_view_margin())continue;
        /* A close fractured abutment, with forty irregular contour sections. */
        int pl=x-40,pr=x+22,py=y-380;
        for(int j=1;j<=40;++j) {
            unsigned h=ht_hash((unsigned)(n*947+j*31));
            int ny=y-380+j*22,l=x-40-(int)(h%11),r=x+22+(int)((h>>8)%15)+j/3;
            ht_triangle(ht_scene,pl,py,pr,py,l,ny,249);
            ht_triangle(ht_scene,pr,py,r,ny,l,ny,249);
            if(j%3==0) {
                ht_limb(ht_scene,r-4,ny-17,r-12,ny-8,0,0,198);
                ht_limb(ht_scene,r-12,ny-8,r-9,ny+3,0,0,214);
            }
            pl=l;pr=r;py=ny;
        }
        /* A weathered vertical return pipe rests on anchored brackets. */
        ht_limb(ht_scene,x+46,y-330,x+49,y+300,8,9,244);
        ht_limb(ht_scene,x+49,y-320,x+52,y+260,1,1,183);
        for(int band=0;band<4;++band) {
            int at=y-260+band*137;
            ht_rect(ht_scene,x+33,at,30,9,251);
            ht_limb(ht_scene,x+16,at+6,x+43,at+6,2,2,246);
            ht_disk(ht_scene,x+37,at+4,2,180);ht_disk(ht_scene,x+58,at+4,2,190);
        }
        /* Sparse grass hangs out of one damp mortar seam. */
        for(int blade=0;blade<7;++blade) {
            int bx=x+24,by=y-35,k=blade*4;
            ht_limb(ht_scene,bx,by,bx+10+k,by-10+k/3,1,0,238);
            ht_limb(ht_scene,bx+10+k,by-10+k/3,bx+16+k,by+5+k/2,0,0,238);
        }
        ht_checkpoint();
    }
}
