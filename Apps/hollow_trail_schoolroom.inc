/* Novella II: an ordinary top-floor schoolroom. The floor and two ladder
 * connections are the existing city route; the desks do not become obstacles.
 * Map/window coordinates derive from that route, not a second invented level. */
#define HT_SCHOOL_MAP_X 535
#define HT_SCHOOL_FOCUSES 3u
static const char *const ht_school_labels[]={"THE LOWERED LADDER","THE CRATE AT THE HIGH CROSSING","THE SIGNAL WINDOW"};
static int ht_school_route_x(unsigned focus) {
    const ht_mechanics *m=&ht_mechanics_by_level[1];
    return focus==0?m->ladders[0].x:focus==1?m->crate_x:m->ladders[2].x;
}
static int ht_school_roof_y(int wx) {
    const ht_platform *land=ht_level_land(1);int top=220;
    for(int i=0;i<HT_PLATFORMS;++i)
        if(wx>=land[i].left && wx<=land[i].right)top=ht_min(top,land[i].top);
    return top;
}
static int ht_school_window_x(int wx) {return 189+ht_clamp(wx,0,HT_GOAL)*238/HT_GOAL;}
static int ht_school_window_y(int wy) {return 63+(wy+100)*67/320;}
static int ht_school_paper_x(int wx) {return 155+ht_clamp(wx,0,HT_GOAL)*163/HT_GOAL;}
static int ht_school_paper_y(int wy) {return 176+(wy+100)*29/320;}
static void ht_school_desk(int x,int floor,int width,int ink) {
    ht_limb(ht_scene,x-width/2,floor-18,x+width/2,floor-21,2,2,ink);
    ht_limb(ht_scene,x-width/2+3,floor-17,x-width/2+4,floor,1,1,ink+15);
    ht_limb(ht_scene,x+width/2-3,floor-20,x+width/2-4,floor,1,1,ink+15);
    ht_rect(ht_scene,x-width/2+6,floor-16,width-12,3,ink-15);
}
static void ht_city_schoolroom(const ht_game *g) {
    if(g->level!=1)return;
    int cx=g->camera/256,cy=g->camera_y/256,x=450-cx,floor=80-cy;
    if(x+180< -ht_view_margin() || x>HT_W+ht_view_margin())return;
    ht_rect(ht_scene,x,floor-124,175,124,114);
    ht_triangle(ht_scene,x-9,floor-125,x+87,floor-140,x+185,floor-122,211);
    ht_limb(ht_scene,x-9,floor-124,x+183,floor-122,2,2,233);
    /* Missing downhill wall exposes the room; rain blooms hang beneath roof. */
    for(int i=0;i<5;++i) {
        int xx=x+18+i*32;
        ht_triangle(ht_scene,xx-11,floor-122,xx+12,floor-122,xx+3,floor-104-(i%3)*4,139);
        ht_triangle(ht_scene,xx-7,floor-122,xx+3,floor-108,xx-5,floor-112,148);
        ht_limb(ht_scene,xx+3,floor-110,xx+4,floor-95-(i%2)*7,1,0,129);
    }
    ht_rect(ht_scene,x+62,floor-106,98,57,26);
    for(int i=0;i<5;++i) {
        int bx=x+65+i*19,top=floor-58-(i*17)%36;
        ht_rect(ht_scene,bx,top,15,floor-49-top,75+i*12);
        ht_limb(ht_scene,bx,top,bx+15,top,0,0,98+i*10);
    }
    ht_rect(ht_scene,x+60,floor-108,102,3,207);
    ht_rect(ht_scene,x+60,floor-108,3,61,207);ht_rect(ht_scene,x+159,floor-108,3,61,207);
    ht_rect(ht_scene,x+60,floor-48,102,4,207);
    ht_rect(ht_scene,x+91,floor-108,2,61,180);ht_rect(ht_scene,x+126,floor-108,2,61,180);
    ht_rect(ht_scene,x+60,floor-77,102,2,180);
    /* Half-erased board, dead stems, small desks: used rather than theatrical. */
    ht_rect(ht_scene,x+10,floor-85,39,40,209);
    ht_rect(ht_scene,x+13,floor-82,33,16,157);
    for(int i=0;i<4;++i)ht_limb(ht_scene,x+17,floor-63+i*4,x+31+i*3,floor-63+i*4,0,0,121);
    for(int i=0;i<3;++i) {
        int xx=x+76+i*27;ht_rect(ht_scene,xx,floor-53,6,6,159);
        ht_limb(ht_scene,xx+3,floor-53,xx+2,floor-66-i*3,0,0,169);
        ht_limb(ht_scene,xx+2,floor-60,xx-2,floor-63,0,0,169);
    }
    ht_triangle(ht_scene,x+65,floor-46,x+151,floor-46,x+132,floor-1,72);
    ht_triangle(ht_scene,x+65,floor-46,x+132,floor-1,x+39,floor-1,72);
    ht_school_desk(x+26,floor,30,181);ht_school_desk(x+138,floor,30,181);
    /* The central map desk shares the actual evidence X and floor. */
    ht_school_desk(HT_SCHOOL_MAP_X-cx,floor,48,197);
    ht_limb(ht_scene,x+109,floor-19,x+109,floor-3,1,1,200);
    ht_limb(ht_scene,x+108,floor-9,x+122,floor-9,2,2,220);
    ht_limb(ht_scene,x+119,floor-9,x+119,floor,1,1,215);
    ht_rect(ht_scene,x+111,floor-7,6,7,190); /* bag below the child's chair */
    ht_limb(ht_scene,x+14,floor-10,x+20,floor-13,0,0,125); /* unfinished carving */
    ht_checkpoint();
}
static void ht_schoolroom_study_render(unsigned focus) {
    focus%=HT_SCHOOL_FOCUSES;
    ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;
    ht_native_foreground_half_y=false;ht_scene=ht_native_active?ht_native_a:ht_scene_low;ht_world_scale=256;
    for(int y=0;y<HT_H;++y){ht_rect(ht_scene,0,y,HT_W,1,88);if((y&15)==15)ht_checkpoint();}
    ht_rect(ht_scene,22,25,436,132,75);ht_rect(ht_scene,0,157,HT_W,HT_H-157,114);
    for(int i=0;i<8;++i)ht_limb(ht_scene,80+i*45,158,22+i*61,270,0,0,136);
    /* Irregular overlapping damp blooms follow the roof leaks. Broad soft
     * margins and a few narrow runs avoid a row of decorative ceiling teeth. */
    static const int leaks[7][3]={{43,18,9},{109,23,17},{176,13,6},{247,26,12},{316,17,7},{377,24,16},{432,15,8}};
    for(unsigned i=0;i<7;++i) {
        int x=leaks[i][0],radius=leaks[i][1],depth=leaks[i][2];
        for(int y=25;y<25+depth;++y) {
            int u=y-25,width=radius-u*radius/(depth+3);
            int edge=(int)(ht_hash(i*71u+(unsigned)y/3u)%5u)-2;
            ht_rect(ht_scene,x-width+edge,y,2*width,1,100);
            if(width>5)ht_rect(ht_scene,x-width+4+edge,y,2*width-8,1,116);
        }
        ht_limb(ht_scene,x+3,25+depth-3,x+5,31+depth,1,0,99);
    }
    ht_rect(ht_scene,40,53,105,71,217);ht_rect(ht_scene,44,57,97,31,160);
    for(int i=0;i<5;++i)ht_limb(ht_scene,52,99+i*4,85+i*8,99+i*4,0,0,126);
    /* The window is a miniature of the real roof platforms and focus anchors. */
    ht_rect(ht_scene,182,43,254,99,24);
    const ht_platform *land=ht_level_land(1);
    for(int i=0;i<HT_PLATFORMS;++i) {
        int l=ht_school_window_x(land[i].left),r=ht_school_window_x(land[i].right);
        int y=ht_school_window_y(land[i].top),tone=85+(i%3)*22;
        ht_rect(ht_scene,l,y,ht_max(2,r-l),142-y,tone);
        ht_limb(ht_scene,l,y,r,y,0,0,tone+38);
        if(i&1)ht_limb(ht_scene,l+3,y,l+3,y-9,0,0,tone+10);
        for(int wx=l+4;wx<r-3;wx+=9)ht_rect(ht_scene,wx,y+6,3,8,tone+23);
    }
    for(unsigned i=0;i<3;++i) {
        int wx=ht_school_route_x(i),x=ht_school_window_x(wx),y=ht_school_window_y(ht_school_roof_y(wx));
        if(i==0){ht_limb(ht_scene,x,y,x,y+18,1,1,213);for(int k=0;k<4;++k)ht_limb(ht_scene,x-3,y+4*k,x+3,y+4*k,0,0,213);}
        else if(i==1)ht_rect(ht_scene,x-3,y-6,6,6,205);
        else {ht_rect(ht_scene,x-4,y-14,8,14,198);ht_rect(ht_scene,x-2,y-11,4,6,47);}
    }
    ht_rect(ht_scene,179,39,260,5,205);ht_rect(ht_scene,179,39,5,107,205);
    ht_rect(ht_scene,435,39,5,107,205);ht_rect(ht_scene,179,142,260,5,215);
    ht_rect(ht_scene,263,43,3,99,177);ht_rect(ht_scene,350,43,3,99,177);
    for(int i=0;i<3;++i) {
        int x=220+i*86;ht_rect(ht_scene,x,135,10,8,172);
        ht_limb(ht_scene,x+5,135,x+3,117-i*4,0,0,189);ht_limb(ht_scene,x+4,129,x-2,122,0,0,189);
    }
    /* Undersized desks recede to either side of the study desk. Quiet rows,
     * not rubble: the map remains the brightest plane and clear focal point. */
    ht_school_desk(51,178,62,162);ht_school_desk(108,161,42,150);
    ht_school_desk(416,177,61,163);ht_school_desk(449,159,36,151);
    ht_limb(ht_scene,28,163,29,183,1,1,173);
    ht_limb(ht_scene,25,176,43,175,1,1,173);
    /* Perspective desk and folded district map, pinned by four real bricks. */
    ht_triangle(ht_scene,121,165,322,155,356,216,153);ht_triangle(ht_scene,121,165,356,216,109,222,153);
    ht_limb(ht_scene,115,223,118,254,3,2,229);ht_limb(ht_scene,347,217,344,254,3,2,229);
    ht_triangle(ht_scene,148,169,313,163,331,209,34);ht_triangle(ht_scene,148,169,331,209,143,217,34);
    /* Folded paper has a worn route crease, not pristine graph-paper lines. */
    ht_limb(ht_scene,227,168,236,212,0,0,79);ht_limb(ht_scene,148,192,322,187,0,0,66);
    for(int i=0;i<5;++i)ht_limb(ht_scene,153,178+i*7,314,174+i*7,0,0,48);
    for(int i=0;i<8;++i)ht_limb(ht_scene,158+i*21,173,160+i*21,205,0,0,51);
    /* Faint district footprints preserve the actual route silhouette below
     * the sister's dark pencil path, rather than an abstract three-dot graph. */
    for(int i=0;i<HT_PLATFORMS;++i) {
        int l=ht_school_paper_x(land[i].left),r=ht_school_paper_x(land[i].right);
        int y=ht_school_paper_y(land[i].top);
        ht_rect(ht_scene,l,y,ht_max(2,r-l),5+(i%3),74);
        ht_limb(ht_scene,l,y,r,y,0,0,108);
        for(int x=l+3;x<r-2;x+=6)ht_rect(ht_scene,x,y+2,2,2,42);
    }
    ht_limb(ht_scene,155,204,310,200,0,0,98);
    for(int i=0;i<4;++i)ht_limb(ht_scene,170+i*39,198,179+i*39,212,0,0,87);
    int px=0,py=0;
    for(unsigned i=0;i<HT_SCHOOL_FOCUSES;++i) {
        int wx=ht_school_route_x(i),x=ht_school_paper_x(wx),y=ht_school_paper_y(ht_school_roof_y(wx));
        if(i)ht_limb(ht_scene,px,py,x,y,1,1,205);
        ht_disk(ht_scene,x,y,2,195);px=x;py=y;
    }
    static const int bricks[4][2]={{144,169},{311,164},{143,209},{323,204}};
    for(int i=0;i<4;++i) {
        int x=bricks[i][0],y=bricks[i][1];ht_rect(ht_scene,x-7,y-3,14,6,187);
        ht_limb(ht_scene,x-6,y-3,x+6,y-3,0,0,126);ht_limb(ht_scene,x+4,y-2,x+6,y+1,0,0,143);
    }
    /* Seated at the undersized desk. The player's trace controls a hand on the
     * paper and a small matching sight mark at the corresponding real roof. */
    int wx=ht_school_route_x(focus),fx=ht_school_paper_x(wx),fy=ht_school_paper_y(ht_school_roof_y(wx));
    /* Close first-person study: the sleeve enters from the near table edge,
     * so the hand never stretches impossibly across a seated silhouette. */
    ht_limb(ht_scene,fx+21,232,fx+5,214,7,4,228);
    ht_limb(ht_scene,fx+5,214,fx-3,fy+5,4,2,222);
    ht_limb(ht_scene,fx-3,fy+5,fx,fy,2,1,209);
    ht_rect(ht_scene,75,215,15,17,201);ht_limb(ht_scene,79,215,85,210,1,1,201);
    int tx=ht_school_window_x(wx),ty=ht_school_window_y(ht_school_roof_y(wx));
    ht_limb(ht_scene,tx-4,ty-5,tx-2,ty-3,0,0,220);ht_limb(ht_scene,tx+4,ty-5,tx+2,ty-3,0,0,220);
    ht_rect(ht_scene,42,230,396,23,255);
    ht_text_color((HT_W-(int)strlen(ht_school_labels[focus])*6)/2,233,ht_school_labels[focus],1,76);
    ht_text_color(58,243,"L/R TRACE   A READ NOTE   X/BACK RETURN",1,102);
    ht_vignette();
}
