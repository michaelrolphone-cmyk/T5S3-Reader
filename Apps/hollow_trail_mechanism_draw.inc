/* Sharp, physical mechanisms over cached landscapes. Fixed geometry budgets;
 * all moving pieces derive from the immutable render snapshot. */
static void ht_mechanism_number(int x,int y,int value) {
    char text[2]={(char)('0'+value),0};ht_text(x,y,text,1);
}
static void ht_mechanism_wheel(int x,int y,int radius,int position) {
    static const int dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};
    ht_disk(ht_scene,x,y,radius,245);ht_disk(ht_scene,x,y,radius-3,35);
    for(int i=0;i<4;++i) ht_limb(ht_scene,x,y,x+dx[i]*(radius-2),y+dy[i]*(radius-2),1,1,180);
    ht_limb(ht_scene,x,y,x+dx[position%4]*(radius-1),y+dy[position%4]*(radius-1),2,2,0);
    ht_disk(ht_scene,x,y,3,255);
}
static void ht_mechanism_arrow(int x,int y,int end) {
    ht_limb(ht_scene,x,y,end,y,1,1,205);
    int sign=end>x?1:-1;
    ht_limb(ht_scene,end-sign*5,y-4,end,y,1,1,205);
    ht_limb(ht_scene,end-sign*5,y+4,end,y,1,1,205);
}
static void ht_draw_optical_bench(const ht_game *g,int x,int y) {
    static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    static const int mx[3]={1,1,5},my[3]={4,1,1};
    ht_rect(ht_scene,x-5,y-5,82,71,215);ht_rect(ht_scene,x-3,y-3,78,67,50);
    int bx=0,by=4,d=0;
    for(int step=0;step<40;++step) {
        int nx=bx+dx[d],ny=by+dy[d];
        if(nx<0 || nx>6 || ny<0 || ny>5) break;
        ht_limb(ht_scene,x+bx*12,y+by*12,x+nx*12,y+ny*12,1,1,0);
        bx=nx;by=ny;if(bx==5 && by==5) break;
        for(int i=0;i<3;++i) if(bx==mx[i] && by==my[i]) {d=ht_ray_turn(d,g->puzzle.value[i]);break;}
        if(d<0) break;
    }
    for(int i=0;i<3;++i) {
        int px=x+mx[i]*12,py=y+my[i]*12,v=g->puzzle.value[i];
        ht_disk(ht_scene,px,py,5,255);
        if(v==1 || v==2) ht_limb(ht_scene,px-3,py+(v==1?3:-3),px+3,py+(v==1?-3:3),1,1,0);
        if(v==3) ht_disk(ht_scene,px,py,3,50);
        ht_mechanism_number(px+6,py-3,i+1);
    }
    ht_disk(ht_scene,x+60,y+60,4,ht_mirror_lit(&g->puzzle)?0:255);
}
/* A different boundary for every chapter: mill loading door -> brick city,
 * rooftop service hatch -> oil pipes, tank bulkhead -> rails, signal gantry ->
 * reeds, lock -> quarry, hoist cage -> glazing, conservatory -> dam masonry,
 * spillway -> ridge, snow shelter -> tower. The final cabinet stays distinct. */
static void ht_draw_chapter_gate(const ht_game *g,int top) {
    int x=HT_GOAL-g->camera/256,open=g->puzzle.opening*2;
    if(x<-150 || x>HT_W+90) return;
    int style=(int)g->level;
    ht_rect(ht_scene,x-23,top-106,7,106,245);ht_rect(ht_scene,x+23,top-106,7,106,245);
    ht_rect(ht_scene,x-28,top-111,63,8,255);
    if(style==0 || style==5 || style==8) {
        ht_limb(ht_scene,x-35,top-110,x+4,top-135,4,3,255);
        ht_limb(ht_scene,x+4,top-135,x+40,top-110,3,4,255);
        for(int i=-18;i<27;i+=11) ht_limb(ht_scene,x+i,top-126,x+i,top-113,1,1,155);
    } else if(style==1 || style==2 || style==3 || style==7) {
        for(int y=top-101;y<top;y+=16) {
            ht_disk(ht_scene,x-20,y,1,0);ht_disk(ht_scene,x+26,y,1,0);
        }
        ht_limb(ht_scene,x-23,top-107,x+29,top-66,2,2,200);
        ht_disk(ht_scene,x-30,top-118,8,240);ht_disk(ht_scene,x-30,top-118,4,45);
    } else if(style==6) {
        ht_disk(ht_scene,x+3,top-106,28,230);ht_disk(ht_scene,x+3,top-106,22,25);
        ht_rect(ht_scene,x-18,top-106,43,29,35);
        ht_limb(ht_scene,x+3,top-132,x+3,top-103,1,1,255);
    }
    if(style==7 || style==8) {
        for(int side=-1;side<=1;side+=2) for(int row=0;row<6;++row) {
            int bx=x+side*32-6,by=top-108+row*18;
            ht_rect(ht_scene,bx,by,15,17,style==7?185:220);
            ht_rect(ht_scene,bx+2,by+2,11,2,100);
            if(style==8 && row==0) ht_rect(ht_scene,bx-2,by-3,19,3,0);
        }
    }
    /* The next landscape is visible through and beyond the boundary. */
    for(int i=0;i<4;++i) {
        int px=x+44+i*17;
        if(style==0) {ht_rect(ht_scene,px,top-62-i*8,14,62+i*8,145);ht_rect(ht_scene,px+4,top-54-i*8,4,6,35);}
        else if(style==1 || style==6) {ht_rect(ht_scene,px,top-52,5,52,160);ht_limb(ht_scene,px,top-52,px+14,top-52,3,3,160);}
        else if(style==2) {ht_rect(ht_scene,px,top-4,11,4,230);ht_limb(ht_scene,x+37,top-8,px+12,top-8,1,1,180);}
        else if(style==3) {ht_limb(ht_scene,px,top,px-8,top-30-i*5,1,1,170);ht_disk(ht_scene,px-8,top-30-i*5,2,160);}
        else if(style==4 || style==7) {ht_limb(ht_scene,px,top,px+7,top-40-i*14,8,2,150);}
        else if(style==5) {ht_limb(ht_scene,px,top,px,top-66,1,1,160);ht_limb(ht_scene,px,top-66,px+17,top-84,1,1,160);}
        else {ht_rect(ht_scene,px,top-30-i*14,3,30+i*14,170);ht_limb(ht_scene,px-5,top-26-i*14,px+7,top-26-i*14,1,1,170);}
    }
    if(g->puzzle.opening<48) {
        int height=96-open;
        if(style==4) { /* Two lock leaves swing back, revealing the cut-stone channel. */
            int width=(48-g->puzzle.opening)/2;
            ht_rect(ht_scene,x-16,top-85,width,85,240);ht_rect(ht_scene,x+24-width,top-85,width,85,230);
            for(int i=0;i<5;++i) ht_limb(ht_scene,x-15,top-12-i*16,x+23,top-12-i*16,1,1,150);
        } else if(style==2) {
            ht_disk(ht_scene,x+3,top-49,47,225);ht_disk(ht_scene,x+3,top-49,40,30);
            int radius=ht_max(1,39-open/2);
            ht_disk(ht_scene,x+3,top-49-open/3,radius,215);
            ht_mechanism_wheel(x+3,top-49-open/3,ht_min(12,radius),g->puzzle.opening/8);
        } else if(style==3) {
            int lift=g->puzzle.opening;
            ht_limb(ht_scene,x-17,top-24,x+23-lift/2,top-24-lift,4,3,240);
            for(int i=0;i<4;++i) ht_disk(ht_scene,x-12+i*(9-lift/12),top-24-i*lift/4,2,50);
            ht_rect(ht_scene,x-22,top-65,10,20,245);ht_disk(ht_scene,x-17,top-60,3,100);
            ht_disk(ht_scene,x-17,top-50,3,0);
        } else if(style==5 || style==6) {
            for(int i=0;i<5;++i) ht_rect(ht_scene,x-16+i*10,top-96,2,height,235);
            for(int y=top-94;y<top-open;y+=18) ht_rect(ht_scene,x-16,y,40,2,230);
            if(style==5) ht_rect(ht_scene,x-16,top-open-5,40,5,255);
            else for(int i=0;i<3;++i) if(height>i*24+24)
                ht_limb(ht_scene,x-14,top-94+i*24,x+22,top-73+i*24,1,1,120);
        } else {
            ht_rect(ht_scene,x-16,top-96,40,height,style==7?180:230);
            for(int i=0;i<5;++i) {
                int y=top-90+i*18-open;
                if(y>=top-96) ht_rect(ht_scene,x-13,y,34,2,style==0 || style==8?110:50);
            }
            if(style==0 || style==8) {
                ht_rect(ht_scene,x+3,top-96,2,height,50);
                if(height>20) ht_limb(ht_scene,x-12,top-94,x+20,top-open-4,2,2,110);
            } else if(style==1) {
                for(int i=0;i<4;++i) if(height>28+i*5) ht_rect(ht_scene,x-7,top-82+i*5,23,2,30);
            }
            if(height>14) ht_rect(ht_scene,x-11,top-open-9,30,4,255);

        }
    }
    /* Cable take-up provides motion feedback without flickering a lamp. */
    ht_limb(ht_scene,x-30,top-118,x-30,top-38-open/2,1,1,235);
    ht_rect(ht_scene,x-35,top-38-open/2,10,17,245);
}
static void ht_draw_mechanisms(const ht_game *g,int top) {
    const ht_puzzle_definition *p=&ht_puzzles[g->level];
    int camera=g->camera/256,first=HT_PUZZLE_FIRST-camera;
    if(first>HT_W+50 || first+210<-50) return;
    const uint8_t *v=g->puzzle.value;
    /* Piping/cables physically connect each control to the exit. */
    for(int i=0;i<3;++i) {
        int x=first+i*HT_PUZZLE_SPACING;
        ht_rect(ht_scene,x-5,top-24,10,24,225);ht_rect(ht_scene,x-13,top-4,26,4,255);
        ht_limb(ht_scene,x,top-8,HT_GOAL-camera,top-8,1,1,130);
        ht_mechanism_number(x-2,top-19,i+1);
        if(p->kind==HT_GEARS) {
            int y=top-55;
            ht_mechanism_wheel(x,y,18,v[i]);
            for(int k=0;k<8;++k) {int a=k&3;ht_rect(ht_scene,x-20+(a%2)*36,y-20+(a/2)*36,5,5,225);}
            if(i<2) {ht_limb(ht_scene,x,y-15,x+HT_PUZZLE_SPACING,y-15,1,1,205);ht_limb(ht_scene,x,y+15,x+HT_PUZZLE_SPACING,y+15,1,1,205);}
            if(i==2) {
                ht_limb(ht_scene,x,y-15,x+26,y-34,1,1,160);
                ht_limb(ht_scene,x+26,y-34,first-25,y-34,1,1,160);
                ht_limb(ht_scene,first-25,y-34,first,y-15,1,1,160);
            }
            static const int slot_x[3]={1,0,-1},slot_y[3]={0,1,0};
            ht_limb(ht_scene,x+slot_x[i]*23,y+slot_y[i]*23,x+slot_x[i]*31,y+slot_y[i]*31,3,3,245);
        } else if(p->kind==HT_VESSELS || p->kind==HT_DISTRIBUTOR) {
            int capacity=p->kind==HT_VESSELS?(i==0?8:i==1?5:3):6;
            ht_rect(ht_scene,x-18,top-35-capacity*5,36,capacity*5+7,230);
            ht_rect(ht_scene,x-15,top-32-capacity*5,30,capacity*5,25);
            ht_rect(ht_scene,x-15,top-32-v[i]*5,30,v[i]*5,155);
            for(int k=1;k<=capacity;++k) ht_rect(ht_scene,x-18,top-32-k*5,7,1,255);
            ht_mechanism_number(x-2,top-44-capacity*5,capacity);
            ht_mechanism_wheel(x,top-29,7,v[i]);
            if(i<2) ht_mechanism_arrow(x+21,top-27,x+45);
            else {
                ht_limb(ht_scene,x+21,top-27,x+31,top+9,2,2,100);
                ht_limb(ht_scene,x+31,top+9,first-28,top+9,2,2,0);
                ht_limb(ht_scene,first-28,top+9,first-28,top-27,2,2,100);
                ht_mechanism_arrow(first-28,top-27,first-18);
            }
            if(p->kind==HT_VESSELS && i<2) { /* Equal-shift marks, not a valve code. */
                ht_rect(ht_scene,x+18,top-54,7,4,245);ht_rect(ht_scene,x+21,top-58,2,12,245);
            }
            if(p->kind==HT_DISTRIBUTOR) {
                int demand=i==0?2:i==1?1:3;
                ht_rect(ht_scene,x+19,top-32-demand*5,7,2,245);
                ht_text(x-15,top-91,i==0?"POWER":i==1?"HEAT":"RIDGE",1);
            }
        } else if(p->kind==HT_CIRCUIT) {
            ht_rect(ht_scene,x-22,top-80,44,47,225);ht_rect(ht_scene,x-19,top-77,38,41,40);
            for(int lane=0;lane<3;++lane) {
                int pair=(int)v[i]-1,out=pair>=0 && (lane==pair || lane==pair+1)?2*pair+1-lane:lane;
                int ink=lane==ht_circuit_lane(&g->puzzle,0,i)?0:lane==ht_circuit_lane(&g->puzzle,1,i)?100:180;
                ht_limb(ht_scene,x-18,top-70+lane*13,x+18,top-70+out*13,1,1,ink);
                if(i<2) ht_limb(ht_scene,x+22,top-70+lane*13,x+63,top-70+lane*13,1,1,180);
            }
            ht_mechanism_wheel(x,top-28,8,v[i]);
        } else if(p->kind==HT_BALANCE) {
            ht_mechanism_wheel(x,top-29,9,v[i]);
            int torque=v[0]*3-v[2]*2;
            int pan=i==0?top-52+torque*3:i==2?top-52-torque*2:top-36;
            ht_rect(ht_scene,x-15,pan,30,3,245);
            for(int k=0;k<v[i];++k) {ht_rect(ht_scene,x-12,pan-9-k*9,24,8,230);ht_rect(ht_scene,x-9,pan-8-k*9,18,1,100);}
        } else if(p->kind==HT_LOCK || p->kind==HT_POINTS) {
            ht_mechanism_wheel(x,top-35,12,v[i]);
            ht_text(x-15,top-59,p->kind==HT_LOCK?(i==0?"ENTRY":i==1?"EXIT":"FILL"):(i==0?"BRAKE":i==1?"SHUNT":"POINT"),1);
        } else if(p->kind==HT_MIRRORS) {
            ht_mechanism_wheel(x,top-34,11,v[i]);
            ht_limb(ht_scene,x,top-47,first+82,top-98,1,1,140);
        } else {
            int height=37+i*12;
            ht_limb(ht_scene,x-12,top-height-12,x+12,top-height-12,2,2,225);
            ht_limb(ht_scene,x,top-height-12,x,top-height+5,1,1,255);
            ht_disk(ht_scene,x,top-height,9,235);ht_rect(ht_scene,x-10,top-height+4,20,3,255);
            ht_rect(ht_scene,x-3,top-height-3,6,5,g->puzzle.progress && p->notes[g->puzzle.progress-1]==i?0:120);
        }
        if(ht_puzzle_near(g)==i) ht_rect(ht_scene,x-12,top+3,24,2,0);
    }
    if(p->kind==HT_BALANCE) {
        int pivot=first+102,y=top-100,tilt=v[0]*3-v[2]*2;
        ht_limb(ht_scene,pivot,top-43,pivot,y,3,2,245);
        ht_limb(ht_scene,pivot-102,y+tilt*3,pivot+68,y-tilt*2,3,3,240);
        ht_limb(ht_scene,pivot-102,y+tilt*3,first,top-52+tilt*3,1,1,150);
        ht_limb(ht_scene,pivot+68,y-tilt*2,first+170,top-52-tilt*2,1,1,150);
        for(int k=0;k<6;++k) ht_disk(ht_scene,pivot-17+k*7,y-18,2,245);
        for(int k=1;k<=3;++k) ht_rect(ht_scene,pivot-k*34,y+tilt*k-4,2,8,0);
        for(int k=1;k<=2;++k) ht_rect(ht_scene,pivot+k*34,y-tilt*k-4,2,8,0);
    } else if(p->kind==HT_MIRRORS) {
        ht_limb(ht_scene,first+46,top-55,first+46,top,2,2,210);
        ht_limb(ht_scene,first+125,top-55,first+125,top,2,2,210);
        ht_draw_optical_bench(g,first+48,top-123);
    }
    else if(p->kind==HT_LOCK) {
        int x=first+30,y=top-115;
        ht_rect(ht_scene,x,y,110,39,220);ht_rect(ht_scene,x+3,y+3,104,33,30);
        ht_rect(ht_scene,x+3,y+26-v[2]*9,104,10+v[2]*9,140);
        ht_rect(ht_scene,x+8,y+4,v[0]?2:7,31,240);ht_rect(ht_scene,x+96,y+4,v[1]?2:7,31,240);
        ht_limb(ht_scene,x-20,y+26,x+6,y+26,2,2,180);ht_limb(ht_scene,x+103,y+8,x+130,y+8,2,2,180);
        int boat=y+22-v[2]*9;ht_limb(ht_scene,x+45,boat,x+70,boat,3,3,245);
    } else if(p->kind==HT_POINTS) {
        int x=first+20,y=top-108;
        ht_limb(ht_scene,x,y,x+132,y,2,2,200);ht_limb(ht_scene,x+56,y,x+132,y-23,2,2,180);
        ht_limb(ht_scene,x+45,y,x+71,y-(v[2]?6:0),3,3,255);
        int wagon=x+30+v[1]*62;ht_rect(ht_scene,wagon,y-22,27,15,230);
        ht_disk(ht_scene,wagon+5,y-4,4,255);ht_disk(ht_scene,wagon+23,y-4,4,255);
        ht_rect(ht_scene,wagon-5,y-14,4,14,v[0]?255:70);
    } else if(p->kind==HT_CIRCUIT) {
        ht_text(first-26,top-98,"FEED",1);ht_text(first+148,top-98,"RETURN",1);
        /* Endpoint shapes identify the reversal while the centre stays heat. */
        ht_disk(ht_scene,first-30,top-70,3,245);ht_rect(ht_scene,first-33,top-47,6,6,245);
        ht_rect(ht_scene,first+196,top-73,6,6,245);ht_disk(ht_scene,first+199,top-44,3,245);
    }
}
/* Small in-world engineering traces beside the evidence; the notebook
 * describes their meaning but no longer supplies switch combinations. */
static void ht_draw_mechanism_trace(const ht_game *g,int item,int x,int y) {
    if(g->level==9) return;
    int left=x-24,top=y-57;
    ht_rect(ht_scene,left-3,top-3,54,36,210);ht_rect(ht_scene,left,top,48,30,35);
    switch(g->level) {
    case 0: { /* Cam and slot: the earlier mill pattern survives in the toy. */
        ht_mechanism_wheel(x,top+15,9,item+1);
        static const int dx[3]={1,0,-1},dy[3]={0,1,0};
        ht_limb(ht_scene,x+dx[item]*12,top+15+dy[item]*12,x+dx[item]*17,top+15+dy[item]*17,2,2,245);break;
    }
    case 1:
        for(int row=0;row<3;++row) ht_limb(ht_scene,left+4,top+5+row*10,left+43,top+25-row*10,1,1,row==1?110:230);
        ht_disk(ht_scene,left+3,top+5,3,245);ht_disk(ht_scene,left+44,top+25,3,245);break;
    case 2:
        for(int i=0;i<3;++i) {int cap=i==0?8:i==1?5:3;ht_rect(ht_scene,left+4+i*15,top+26-cap*2,9,cap*2,220);ht_mechanism_number(left+6+i*15,top+2,cap);}
        break;
    case 3:
        ht_limb(ht_scene,left+2,top+25,left+46,top+25,1,1,230);
        ht_limb(ht_scene,left+22,top+25,left+46,top+12,1,1,230);
        ht_rect(ht_scene,left+12,top+8,18,12,245);ht_rect(ht_scene,left+9,top+12,3,13,130);break;
    case 4:
        ht_limb(ht_scene,left+2,top+24,left+18,top+24,2,2,170);
        ht_limb(ht_scene,left+28,top+5,left+46,top+5,2,2,170);
        ht_rect(ht_scene,left+18,top+10,3,18,245);ht_rect(ht_scene,left+27,top+3,3,25,245);
        ht_mechanism_arrow(left+6,top+15,left+13);break;
    case 5:
        ht_limb(ht_scene,left+25,top+10,left+25,top+26,2,2,230);
        ht_limb(ht_scene,left+4,top+10,left+39,top+10,2,2,230);
        for(int i=0;i<6;++i) ht_disk(ht_scene,left+5+i*7,top+3,2,245);
        ht_mechanism_number(left+9,top+18,3);ht_mechanism_number(left+31,top+18,2);break;
    case 6:
        ht_limb(ht_scene,left+4,top+21,left+25,top+21,1,1,245);
        ht_limb(ht_scene,left+25,top+21,left+25,top+3,1,1,245);
        ht_limb(ht_scene,left+20,top+26,left+30,top+16,2,2,160);break;
    case 7:
        for(int i=0;i<3;++i) {int demand=i==0?2:i==1?1:3;
            ht_rect(ht_scene,left+4+i*15,top+4,8,23,100);
            for(int k=0;k<demand;++k) ht_rect(ht_scene,left+3+i*15,top+23-k*6,10,2,245);
        }break;
    case 8: {
        static const int heights[4]={17,9,25,17};
        for(int i=0;i<4;++i) {int px=left+5+i*12;
            ht_rect(ht_scene,px,top+28-heights[i],3,heights[i],230);
            ht_rect(ht_scene,px-2,top+30-heights[i],7,2,0);
        }
        ht_limb(ht_scene,left+2,top+13,left-3,top+20,2,1,220);break;
    }
    }
}
