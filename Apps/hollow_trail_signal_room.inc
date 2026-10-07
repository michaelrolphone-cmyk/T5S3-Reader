/* Novella II, set 11: the signal is a lamp, a wheel and a metal tongue.
 * This is dressing on city parcel 7, never additional collision geometry.
 * Its window, lamp and study views share the rain-tank signal's 144-tick phase.
 * All geometry uses the ordinary ink primitives, including native raster mode. */
#define HT_SIGNAL_ROOM_FOCUSES 3u
static const char *const ht_signal_room_labels[HT_SIGNAL_ROOM_FOCUSES]={
    "THE WHEEL TURNS", "TWO FACING PAGES", "THE BLOTS FIT EXACTLY"
};
static const int16_t ht_signal_circle[32]={
    0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,
    0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50
};
/* A slow, continuously advancing wheel, rather than a second flashing clock.
 * The three contact lifts are mechanically tied to the existing signal phase. */
/* scale is in half-size units: 1 for the small world machine, 4 for the
 * close seated view. Thin contacts remain at least one raster line. */
static void ht_signal_machine(int x,int y,int scale,unsigned ticks) {
#define HT_SIGNAL_UNIT(n) ((n)*scale/2)
    int r=HT_SIGNAL_UNIT(17),phase=(int)((ticks%144u)*32u/144u);
    int fine=ht_max(1,HT_SIGNAL_UNIT(1));
    bool lit=ht_rain_window_lit(ticks);
    /* Small bolted wall bracket carries the lamp beside the floor wheel. */
    ht_rect(ht_scene,x-HT_SIGNAL_UNIT(35),y-HT_SIGNAL_UNIT(28),HT_SIGNAL_UNIT(8),HT_SIGNAL_UNIT(38),184);
    ht_disk(ht_scene,x-HT_SIGNAL_UNIT(31),y-HT_SIGNAL_UNIT(25),fine,224);
    ht_disk(ht_scene,x-HT_SIGNAL_UNIT(31),y+HT_SIGNAL_UNIT(6),fine,224);
    ht_limb(ht_scene,x-HT_SIGNAL_UNIT(8),y+HT_SIGNAL_UNIT(5),x-HT_SIGNAL_UNIT(13),y+HT_SIGNAL_UNIT(49),HT_SIGNAL_UNIT(2),HT_SIGNAL_UNIT(3),227);
    ht_limb(ht_scene,x+HT_SIGNAL_UNIT(7),y+HT_SIGNAL_UNIT(5),x+HT_SIGNAL_UNIT(15),y+HT_SIGNAL_UNIT(49),HT_SIGNAL_UNIT(2),HT_SIGNAL_UNIT(3),223);
    ht_limb(ht_scene,x-HT_SIGNAL_UNIT(19),y+HT_SIGNAL_UNIT(49),x+HT_SIGNAL_UNIT(22),y+HT_SIGNAL_UNIT(49),HT_SIGNAL_UNIT(2),HT_SIGNAL_UNIT(2),237);
    ht_disk(ht_scene,x,y,r,231);ht_disk(ht_scene,x,y,r-HT_SIGNAL_UNIT(3),106);
    for(int tooth=0;tooth<16;++tooth) {
        unsigned a=(unsigned)(phase+tooth*2)&31u;
        int sx=ht_signal_circle[(a+8u)&31u],sy=ht_signal_circle[a];
        ht_limb(ht_scene,x+sx*(r-fine)/256,y+sy*(r-fine)/256,
                x+sx*(r+HT_SIGNAL_UNIT(2))/256,y+sy*(r+HT_SIGNAL_UNIT(2))/256,
                HT_SIGNAL_UNIT(1),HT_SIGNAL_UNIT(1),lit?44:170);
    }
    for(int spoke=0;spoke<5;++spoke) {
        unsigned a=(unsigned)(phase+spoke*6)&31u;
        ht_limb(ht_scene,x,y,x+ht_signal_circle[(a+8u)&31u]*(r-HT_SIGNAL_UNIT(4))/256,
                y+ht_signal_circle[a]*(r-HT_SIGNAL_UNIT(4))/256,HT_SIGNAL_UNIT(1),HT_SIGNAL_UNIT(1),219);
    }
    ht_disk(ht_scene,x,y,HT_SIGNAL_UNIT(4),232);ht_disk(ht_scene,x-fine,y-fine,fine,lit?38:145);
    /* The oil bead stays attached beneath the fixed axle, never falling. */
    ht_limb(ht_scene,x+HT_SIGNAL_UNIT(3),y+HT_SIGNAL_UNIT(3),x+HT_SIGNAL_UNIT(3),y+HT_SIGNAL_UNIT(8),HT_SIGNAL_UNIT(1),0,246);
    ht_disk(ht_scene,x+HT_SIGNAL_UNIT(3),y+HT_SIGNAL_UNIT(8),fine,245);
    ht_limb(ht_scene,x-HT_SIGNAL_UNIT(30),y+HT_SIGNAL_UNIT(16),x-HT_SIGNAL_UNIT(30),y-HT_SIGNAL_UNIT(11),HT_SIGNAL_UNIT(2),HT_SIGNAL_UNIT(1),241);
    ht_limb(ht_scene,x-HT_SIGNAL_UNIT(34),y+HT_SIGNAL_UNIT(16),x-HT_SIGNAL_UNIT(10),y+HT_SIGNAL_UNIT(16),HT_SIGNAL_UNIT(1),HT_SIGNAL_UNIT(1),218);
    /* Contact tongue rises, holds and falls at each of the three flashes. */
    int lift=lit?HT_SIGNAL_UNIT(6):0;
    ht_limb(ht_scene,x-HT_SIGNAL_UNIT(27),y+HT_SIGNAL_UNIT(10),x-HT_SIGNAL_UNIT(16),y+HT_SIGNAL_UNIT(8)-lift,HT_SIGNAL_UNIT(1),0,lit?34:190);
    ht_rect(ht_scene,x-HT_SIGNAL_UNIT(22),y+HT_SIGNAL_UNIT(1),HT_SIGNAL_UNIT(4),HT_SIGNAL_UNIT(3),239);
    ht_limb(ht_scene,x-HT_SIGNAL_UNIT(27),y+HT_SIGNAL_UNIT(11),x-HT_SIGNAL_UNIT(34),y+HT_SIGNAL_UNIT(22),0,0,172);
    /* Shallow enamel shade, glass and holder: an electric lamp, not a flame. */
    ht_disk(ht_scene,x-HT_SIGNAL_UNIT(31),y-HT_SIGNAL_UNIT(13),HT_SIGNAL_UNIT(7),232);
    ht_disk(ht_scene,x-HT_SIGNAL_UNIT(31),y-HT_SIGNAL_UNIT(13),HT_SIGNAL_UNIT(5),lit?8:153);
    ht_triangle(ht_scene,x-HT_SIGNAL_UNIT(42),y-HT_SIGNAL_UNIT(19),x-HT_SIGNAL_UNIT(23),y-HT_SIGNAL_UNIT(21),x-HT_SIGNAL_UNIT(19),y-HT_SIGNAL_UNIT(14),231);
    ht_limb(ht_scene,x-HT_SIGNAL_UNIT(41),y-HT_SIGNAL_UNIT(19),x-HT_SIGNAL_UNIT(24),y-HT_SIGNAL_UNIT(21),HT_SIGNAL_UNIT(1),HT_SIGNAL_UNIT(1),lit?100:185);
    ht_rect(ht_scene,x-HT_SIGNAL_UNIT(35),y-HT_SIGNAL_UNIT(7),HT_SIGNAL_UNIT(9),HT_SIGNAL_UNIT(3),244);
#undef HT_SIGNAL_UNIT
}
static void ht_signal_rush_chair(int x,int floor,bool lit) {
    /* Adult-scale to the 28-pixel protagonist: seat 11, back 26 pixels.
     * Profile faces the window to the left; the back is visibly empty. */
    ht_limb(ht_scene,x+6,floor-26,x+5,floor,1,1,222);
    ht_limb(ht_scene,x-6,floor-11,x-7,floor,1,1,224);
    ht_limb(ht_scene,x-6,floor-12,x+6,floor-12,1,1,222);
    ht_limb(ht_scene,x+6,floor-25,x-1,floor-25,1,1,224);
    ht_limb(ht_scene,x+5,floor-20,x,floor-20,1,0,lit?79:179);
    ht_triangle(ht_scene,x-6,floor-13,x+5,floor-13,x+7,floor-11,lit?64:150);
    ht_triangle(ht_scene,x-6,floor-13,x+7,floor-11,x-5,floor-10,lit?64:150);
    /* Rush strands sag into a used seat; no cushion or invented occupant. */
    for(int i=0;i<5;++i) {
        int sx=x-5+i*2,drop=2-ht_abs(i-2);
        ht_limb(ht_scene,sx,floor-13,sx+1,floor-12+drop,0,0,lit?142:202);
    }
    ht_limb(ht_scene,x-5,floor-5,x+5,floor-5,1,1,195);
}
/* This entry bay crosses the playable roof from its outer landing into
 * the cutaway room. Both casements open in depth; their projected leaves
 * narrow to the shared jambs. The collision uses these exact frame edges. */
static void ht_signal_window_draw(const ht_game *g,bool front) {
    if(g->level!=1)return;
    int cx=g->camera/256,cy=g->camera_y/256,left=HT_SIGNAL_WINDOW_LEFT-cx,right=HT_SIGNAL_WINDOW_RIGHT-cx;
    if(right< -ht_view_margin() || left>HT_W+ht_view_margin())return;
    int floor=ht_signal_window_floor()-cy,sill=ht_signal_window_sill()-cy,top=ht_signal_window_top()-cy;
    bool lit=ht_rain_window_lit(g->ticks);
    if(front) {
        /* Near framing occludes the body while the feet use the same sill. */
        ht_rect(ht_scene,left,sill,right-left,floor-sill,153);
        ht_limb(ht_scene,left-2,sill,right+2,sill,1,1,lit?67:159);
        ht_limb(ht_scene,left,sill+3,right,sill+3,0,0,190);
        ht_rect(ht_scene,left,top,5,sill-top,217);
        ht_rect(ht_scene,right-5,top,5,sill-top,222);
        ht_rect(ht_scene,left,floor-84,right-left,14,193);
        if(g->traversal.mode==HT_WINDOW) {
            /* Contact toes sit on top of the near sill highlight, rather
             * than being erased by its foreground paint. */
            ht_person_pose p=ht_person_pose_at(g->x/256-cx,g->y/256-cy,g);
            for(int i=0;i<2;++i)if(p.foot[i].y==sill && p.foot[i].x>=left && p.foot[i].x<=right) {
                int f=p.facing;ht_joint foot=p.foot[i];
                ht_triangle(ht_scene,foot.x-f,foot.y,foot.x,foot.y-2,foot.x+f*3,foot.y,255);
            }
        }
        return;
    }
    ht_rect(ht_scene,left,top,right-left,sill-top,220);
    ht_rect(ht_scene,left+5,top+3,right-left-10,sill-top-3,lit?49:161);
    int half=(right-left-10)/2,width=half*(32-g->traversal.window_open)/32;
    if(!width) {
        /* Open casements retain their edge-on stiles and cross rails in
         * depth. The aperture is clear, but the window never becomes a
         * frameless doorway. These are the existing leaves, not new props. */
        for(int side=0;side<2;++side) {
            int hinge=side?right-5:left+5,edge=hinge+(side?-4:4);
            ht_limb(ht_scene,hinge,top+3,edge,top+8,0,0,207);
            ht_limb(ht_scene,edge,top+8,edge,sill-5,1,1,207);
            ht_limb(ht_scene,edge,sill-5,hinge,sill-1,0,0,207);
            ht_limb(ht_scene,hinge,floor-45,edge,floor-44,1,1,214);
        }
    }
    if(width) {
        ht_rect(ht_scene,left+5,top+3,width,sill-top-3,lit?36:178);
        ht_rect(ht_scene,right-5-width,top+3,width,sill-top-3,lit?36:178);
        ht_limb(ht_scene,left+5+width,top+3,left+5+width,sill-1,1,1,207);
        ht_limb(ht_scene,right-5-width,top+3,right-5-width,sill-1,1,1,207);
        ht_limb(ht_scene,left+5,floor-45,left+5+width,floor-45,1,1,214);
        ht_limb(ht_scene,right-5-width,floor-45,right-5,floor-45,1,1,214);
        if(width>=4)for(int streak=0;streak<3;++streak) {
            int edge=streak?right-5-width:left+5;
            int px=edge+(streak==2?11:4)*width/half;
            ht_limb(ht_scene,px,floor-63,ht_max(edge,px-1),floor-52,0,0,lit?69:158);
        }
        if(g->traversal.window_print) {
            int px=left+5,py=floor-22,ink=lit?107:174;
            ht_rect(ht_scene,px,py,ht_max(1,3*width/half),2,ink);
            for(int i=0;i<4;++i){int fx=px+i*width/half;ht_limb(ht_scene,fx,py,fx,py-3-(i%3),0,0,ink);}
        }
    }
}
static void ht_city_signal_room(const ht_game *g) {
    if(g->level!=1)return;
    int cx=g->camera/256,cy=g->camera_y/256;
    int left=HT_SIGNAL_ROOM_LEFT-cx,right=HT_SIGNAL_ROOM_RIGHT-cx;
    int floor=ht_level_land(1)[7].top-cy,top=floor-HT_SIGNAL_ROOM_HEIGHT;
    if(right< -ht_view_margin() || left>HT_W+ht_view_margin())return;
    int chair=2155-cx,log=HT_SIGNAL_LOG_X-cx;
    bool lit=ht_rain_window_lit(g->ticks);
    /* Recessed wall and cutaway near side expose the room. Its left entry
     * bay is the physical window; the right passage remains open. */
    ht_rect(ht_scene,left,top,right-left,HT_SIGNAL_ROOM_HEIGHT,153);
    ht_triangle(ht_scene,left,top,left+14,top+9,left+14,floor,187);
    ht_triangle(ht_scene,left,top,left+14,floor,left,floor,187);
    ht_rect(ht_scene,right-20,top+8,20,HT_SIGNAL_ROOM_HEIGHT-8,180);
    ht_limb(ht_scene,left,top,right,top,1,1,215);
    ht_limb(ht_scene,left+1,top+2,right-2,top+2,0,0,117);
    /* Broad worn plaster planes and two quiet seams give the cutaway depth.
     * No random dirt texture or decorative ledges compete with the chair. */
    ht_triangle(ht_scene,left+15,top+8,right-23,top+8,right-23,top+45,145);
    ht_triangle(ht_scene,left+15,top+8,right-23,top+45,left+15,top+23,145);
    ht_limb(ht_scene,left+92,top+14,left+95,top+29,0,0,137);
    ht_limb(ht_scene,left+95,top+29,left+89,top+38,0,0,137);
    ht_limb(ht_scene,right-24,top+7,right-24,floor-8,0,0,126);
    for(int row=0;row<4;++row) {
        int yy=top+18+row*17;
        ht_limb(ht_scene,right-19,yy,right-2,yy,0,0,161);
        ht_limb(ht_scene,right-12+(row%2)*5,yy,right-12+(row%2)*5,yy-15,0,0,164);
    }
    ht_signal_window_draw(g,false);
    /* Bare floor and one changing pool of light. Narrow seams remain surface
     * marks; all chair legs and machine feet end on parcel 7's actual top. */
    ht_rect(ht_scene,left+14,floor-5,right-left-34,5,lit?94:142);
    if(lit) {
        ht_triangle(ht_scene,2175-cx,floor-9,chair-30,floor-1,chair+33,floor-1,64);
        ht_limb(ht_scene,chair+6,floor-2,chair-9,floor-2,0,0,215);
    }
    for(int i=0;i<5;++i)ht_limb(ht_scene,left+25+i*32,floor-4,left+30+i*32,floor-1,0,0,lit?116:166);
    /* Single empty hook above the chair. */
    ht_rect(ht_scene,chair+3,floor-45,5,6,191);
    ht_limb(ht_scene,chair+5,floor-44,chair+5,floor-38,1,1,238);
    ht_limb(ht_scene,chair+5,floor-38,chair+2,floor-37,1,1,238);
    ht_limb(ht_scene,chair+2,floor-37,chair+1,floor-40,1,1,238);
    ht_signal_rush_chair(chair,floor,lit);
    ht_signal_machine(2190-cx,floor-25,1,g->ticks);
    /* Open watch log lies beside the chair, exactly at the evidence anchor. */
    ht_triangle(ht_scene,log-7,floor-4,log,floor-3,log,floor-1,lit?45:91);
    ht_triangle(ht_scene,log-7,floor-4,log,floor-1,log-7,floor-2,lit?45:91);
    ht_triangle(ht_scene,log,floor-3,log+6,floor-4,log+7,floor-2,lit?36:84);
    ht_triangle(ht_scene,log,floor-3,log+7,floor-2,log,floor-1,lit?36:84);
    ht_limb(ht_scene,log,floor-3,log,floor-1,0,0,214);
    for(int i=0;i<2;++i) {
        ht_limb(ht_scene,log-5+i*2,floor-3,log-4+i*2,floor-2,0,0,171);
        ht_limb(ht_scene,log+2+i*2,floor-3,log+3+i*2,floor-2,0,0,174);
    }
    ht_checkpoint();
}
/* Quiet rain and towers seen from inside. No scene-wide random noise or camera
 * movement: after recognition the only moving room object is the mechanism. */
static void ht_signal_study_window(int x,int y,int w,int h) {
    ht_rect(ht_scene,x,y,w,h,28);
    for(int i=0;i<7;++i) {
        int bx=x+5+i*(w-12)/7,bw=(w-12)/7-7,by=y+24+(i*19)%53;
        int ink=58+(i%3)*17;
        ht_rect(ht_scene,bx,by,bw,y+h-by,ink);
        ht_limb(ht_scene,bx-2,by,bx+bw+1,by,0,0,ink+16);
        if(i&1)ht_rect(ht_scene,bx+bw-7,by-12,3,12,ink+7);
        for(int row=0;row<4;++row)for(int col=0;col<2;++col) {
            int wy=by+9+row*19;if(wy+6>=y+h)continue;
            ht_rect(ht_scene,bx+5+col*ht_max(6,(bw-9)/2),wy,3,6,(row+i+col)%3==0?14:ink+12);
        }
    }
    for(int i=0;i<23;++i) {
        int rx=x+5+(i*37)%(w-10),ry=y+5+(i*29)%(h-25);
        ht_limb(ht_scene,rx,ry,rx-2,ry+12+(i%4),0,0,49);
    }
    ht_rect(ht_scene,x-5,y-5,w+10,5,229);ht_rect(ht_scene,x-5,y-5,5,h+10,225);
    ht_rect(ht_scene,x+w,y-5,5,h+10,227);ht_rect(ht_scene,x-7,y+h,w+14,7,213);
    ht_rect(ht_scene,x+w/2-2,y,4,h,198);ht_rect(ht_scene,x,y+h/2,w,3,198);
    ht_limb(ht_scene,x-4,y+h+1,x+w+4,y+h+1,0,0,112);
}
static void ht_signal_open_hand(int x,int y,int side) {
    /* Sleeves enter from the viewer, with solid palms and five separate digits.
     * side points the thumb inward; fingers stay in front of the coat cuff. */
    ht_limb(ht_scene,x-side*37,243,x-side*15,y+19,13,9,244);
    ht_limb(ht_scene,x-side*15,y+19,x-side*6,y+8,9,7,229);
    ht_limb(ht_scene,x-side*8,y+10,x-side*3,y+3,7,6,185);
    ht_triangle(ht_scene,x-8,y+6,x+6,y+5,x+5,y-8,197);
    ht_triangle(ht_scene,x-8,y+6,x+5,y-8,x-6,y-10,197);
    for(int i=0;i<4;++i) {
        int fx=x-6+i*4;
        ht_limb(ht_scene,fx,y-5,fx+(i-2)*2,y-17+ht_abs(i-1)*2,2,1,193);
    }
    ht_limb(ht_scene,x+side*5,y+1,x+side*14,y-7,3,2,193);
    ht_limb(ht_scene,x-side*6,y+8,x+side*3,y+5,0,0,131);
}
static void ht_signal_seated_study(unsigned ticks) {
    bool lit=ht_rain_window_lit(ticks);
    ht_rect(ht_scene,25,25,430,132,145);
    ht_triangle(ht_scene,25,25,58,40,58,157,172);
    ht_triangle(ht_scene,25,25,58,157,25,184,172);
    ht_rect(ht_scene,24,154,431,79,130);
    /* Boards converge at the window. Nothing occupies the bare room floor. */
    for(int i=0;i<8;++i)ht_limb(ht_scene,76+i*42,155,17+i*65,235,0,0,151);
    ht_limb(ht_scene,33,184,447,184,0,0,143);ht_limb(ht_scene,30,214,450,214,0,0,145);
    ht_signal_study_window(70,42,200,101);
    if(lit) {
        ht_triangle(ht_scene,323,75,69,154,110,228,68);
        ht_triangle(ht_scene,323,75,110,228,350,228,68);
        /* Hands cast long shadows between the viewer's feet. */
        ht_limb(ht_scene,179,166,214,217,8,12,186);
        ht_limb(ht_scene,294,166,257,223,8,12,188);
        for(int i=0;i<4;++i) {
            ht_limb(ht_scene,211+i*4,204,210+i*4,222+(i%2)*4,1,2,186);
            ht_limb(ht_scene,251+i*4,205,247+i*4,224+(i%2)*3,1,2,188);
        }
    }
    /* The fixed axle, hanging oil and tongue can be watched from the chair.
     * The lamp is beside the viewer's head, not a free-floating interface. */
    ht_signal_machine(386,98,4,ticks);
    ht_rect(ht_scene,436,41,3,111,211);ht_limb(ht_scene,385,144,438,152,0,0,196);
    /* Knees and boots establish an occupied first-person rush-seat viewpoint. */
    ht_limb(ht_scene,147,232,165,206,16,12,232);
    ht_limb(ht_scene,334,233,316,207,16,12,232);
    ht_limb(ht_scene,165,207,180,215,10,8,247);ht_limb(ht_scene,316,207,300,216,10,8,247);
    ht_limb(ht_scene,178,212,188,212,3,2,221);ht_limb(ht_scene,301,213,292,213,3,2,221);
    ht_signal_open_hand(178,174,1);ht_signal_open_hand(299,174,-1);
    /* Coat drips have ceased; just their still marks survive at the boots. */
    ht_limb(ht_scene,159,223,180,223,0,0,104);ht_limb(ht_scene,300,224,320,224,0,0,104);
    ht_checkpoint();
}
/* Broken downstroke and impatient hook/blot are a single shared piece of ink
 * geometry. The duplicated entry is literally drawn at the same coordinates
 * when the physical sheets are aligned; it is not a symbolic matching icon. */
static void ht_signal_signature(int x,int y,unsigned variant,int ink) {
    static const int8_t stroke[][2]={
        {0,9},{5,1},{10,2},{8,12},{13,7},{18,9},{21,6},{24,9},{27,7},
        {31,0},{30,9},{30,12},{32,15},{36,7},{40,9},{44,6},{47,8},{50,7},{52,11},{58,8},{65,9}
    };
    for(unsigned i=1;i<sizeof(stroke)/sizeof(stroke[0]);++i) {
        if(i==11)continue; /* characteristic interruption near the downstroke */
        int dy=variant && i>12?(int)(variant%3u)-1:0;
        ht_limb(ht_scene,x+stroke[i-1][0],y+stroke[i-1][1]+dy,
                x+stroke[i][0],y+stroke[i][1]+dy,0,0,ink);
    }
    if(!variant) {
        /* Only the duplicated second entries carry this exact impatient blot.
         * The other five signatures must not manufacture seven matching blots. */
        ht_disk(ht_scene,x+48,y+8,2,ink);
        ht_rect(ht_scene,x+49,y+9,3,2,ink);
        ht_limb(ht_scene,x+52,y+10,x+54,y+13,0,0,ink);
        ht_limb(ht_scene,x+54,y+13,x+58,y+10,0,0,ink);
    }
}
static void ht_signal_page_paper(int x,int y,int ink) {
    ht_triangle(ht_scene,x,y+3,x+118,y,x+122,y+151,ink);
    ht_triangle(ht_scene,x,y+3,x+122,y+151,x+4,y+155,ink);
    /* Same tower geometry and light positions as the 365x177 window behind
     * these sheets, clipped to the paper rather than invented watermark art. */
    for(int i=0;i<7;++i) {
        int bx=63+i*353/7,bw=353/7-7,by=55+(i*19)%53;
        int left=ht_max(x+6,bx),right=ht_min(x+115,bx+bw);
        int top=ht_max(y+7,by),bottom=ht_min(y+147,208);
        if(left<right && top<bottom)ht_rect(ht_scene,left,top,right-left,bottom-top,ink+19);
        for(int row=0;row<4;++row)for(int col=0;col<2;++col) {
            int wx=bx+5+col*ht_max(6,(bw-9)/2),wy=by+9+row*19;
            if(wx>x+6 && wx+3<x+115 && wy>y+7 && wy+6<y+147)
                ht_rect(ht_scene,wx,wy,3,6,(row+i+col)%3==0?ht_max(8,ink-15):ink+25);
        }
    }
    if(x+6<241 && x+115>241)ht_rect(ht_scene,238,y+7,4,140,ink+35);
    if(y+7<119 && y+147>121)ht_rect(ht_scene,x+6,119,109,3,ink+35);
    ht_limb(ht_scene,x,y+3,x+118,y,0,0,96);
    ht_limb(ht_scene,x+118,y,x+122,y+151,0,0,109);
    ht_limb(ht_scene,x+4,y+155,x+122,y+151,0,0,120);
    /* A damp edge bends away from the glass; no animated paper flutter. */
    ht_triangle(ht_scene,x+108,y+151,x+122,y+142,x+122,y+151,122);
    ht_limb(ht_scene,x+8,y+150,x+44,y+149,0,0,82);
}
static void ht_signal_page_ink(int x,int y,unsigned page,int ink,bool prose) {
    static const int rows[4]={20,49,77,98};
    for(unsigned row=0;row<(page?4u:3u);++row) {
        int yy=y+rows[row];
        /* The second entry is duplicated; all seven signatures are present. */
        ht_signal_signature(x+16,yy,row==1?0u:1u+page+row,ink);
        ht_limb(ht_scene,x+15,yy+17,x+102,yy+17,0,0,93);
    }
    if(!prose)return; /* The rear leaf's prose is occluded by the front sheet. */
    if(!page) {
        ht_text_color(x+10,y+120,"ASK WHO ATTENDED,",1,ink);
        ht_text_color(x+10,y+130,"NOT WHOSE NAME",1,ink);
        ht_text_color(x+10,y+140,"APPEARS.",1,ink);
    } else {
        ht_text_color(x+11,y+120,"NOBODY SHOULD",1,ink);
        ht_text_color(x+11,y+130,"KEEP THE WHOLE",1,ink);
        ht_text_color(x+11,y+140,"WATCH ALONE.",1,ink);
    }
}
static void ht_signal_page_hand(int x,int y,int side) {
    ht_limb(ht_scene,x+side*49,240,x+side*17,y+18,13,8,242);
    ht_limb(ht_scene,x+side*17,y+18,x+side*4,y+1,8,5,213);
    ht_limb(ht_scene,x+side*4,y+1,x,y-11,5,3,190);
    ht_limb(ht_scene,x,y-11,x-side*4,y-17,3,2,195); /* thumb on the paper */
    ht_limb(ht_scene,x+side*5,y-2,x+side*6,y-23,2,1,201);
    ht_limb(ht_scene,x+side*8,y+2,x+side*9,y-17,2,1,207);
    ht_limb(ht_scene,x+side*8,y+9,x+side*12,y+14,0,0,146);
}
static void ht_signal_paper_study(unsigned focus) {
    ht_rect(ht_scene,25,22,430,204,148);
    ht_signal_study_window(58,31,365,177);
    ht_rect(ht_scene,31,216,418,18,126);ht_limb(ht_scene,58,215,423,215,0,0,100);
    if(focus==1) {
        /* Separate facing leaves: three signatures and four. The matching
         * broken stroke/blot is at identical positions within either page. */
        ht_signal_page_paper(94,45,44);ht_signal_page_ink(94,45,0,216,true);
        ht_signal_page_paper(264,45,44);ht_signal_page_ink(264,45,1,216,true);
        ht_signal_page_hand(97,173,-1);ht_signal_page_hand(383,173,1);
    } else {
        /* Two distinct paper edges and two hands, but exactly registered ink.
         * A faint lower sheet survives through the front sheet against glass. */
        ht_signal_page_paper(174,43,68);
        ht_signal_page_paper(178,43,39);
        ht_signal_page_ink(178,43,0,135,false);
        ht_signal_page_ink(178,43,1,211,true);
        ht_signal_signature(194,92,0,225);
        ht_signal_page_hand(178,173,-1);ht_signal_page_hand(298,173,1);
    }
    ht_checkpoint();
}
static void ht_signal_room_study_render(unsigned focus,unsigned ticks) {
    focus%=HT_SIGNAL_ROOM_FOCUSES;
    ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;
    ht_native_foreground_half_y=false;
    ht_scene=ht_native_active?ht_native_a:ht_scene_low;ht_world_scale=256;
    for(int y=0;y<HT_H;++y) {
        ht_rect(ht_scene,0,y,HT_W,1,117);
        if((y&15)==15)ht_checkpoint();
    }
    if(focus==0)ht_signal_seated_study(ticks);else ht_signal_paper_study(focus);
    ht_rect(ht_scene,42,230,396,23,255);
    ht_text_color((HT_W-(int)strlen(ht_signal_room_labels[focus])*6)/2,233,ht_signal_room_labels[focus],1,76);
    ht_text_color(43,243,"L/R COMPARE   A READ LOG   X/BACK RETURN",1,102);
    ht_vignette();
}
