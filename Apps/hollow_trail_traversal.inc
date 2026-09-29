/* Fixed-point, allocation-free traversal physics. Geometry and mechanics share
 * chapter coordinates; no completion flags or invisible barriers replace them. */
static int ht_ladder_x(unsigned i) { return i==0?1000:i==1?1600:2190; }
static void ht_ladder_ends(unsigned level,unsigned i,int *top,int *bottom) {
    const ht_platform *land=ht_level_land(level);
    *top=land[i==0?3:i==1?4:7].top;
    *bottom=land[i==0?2:i==1?5:6].top;
}
static int ht_ladder_near(const ht_game *g) {
    for(unsigned i=0;i<3;++i) {
        int top,bottom; ht_ladder_ends(g->level,i,&top,&bottom);
        if(ht_abs(g->x/256-ht_ladder_x(i))<=18 && g->y/256>=top-8 && g->y/256<=bottom+8) return (int)i;
    }
    return -1;
}
static void ht_rope_point(const ht_game *g,int *x,int *feet) {
    int dx=g->traversal.rope_x/256;
    *x=1285+dx;
    *feet=ht_level_land(g->level)[3].top-155+(int)ht_isqrt((unsigned)(210*210-dx*dx))+20;
}
static int ht_traversal_near(const ht_game *g) {
    const ht_traversal *t=&g->traversal;
    if(t->mode) return t->mode;
    int x=g->x/256,y=g->y/256,ground=ht_level_land(g->level)[0].top;
    if(!t->ball_phase && g->grounded && ht_abs(x-t->ball_x/256)<=68 && ht_abs(y-ground)<4) return HT_ROLL;
    if(g->grounded && ht_abs(x-t->crate_x/256)<=40 && ht_abs(y-ground)<4) return HT_CRATE;
    int rx,ry;ht_rope_point(g,&rx,&ry);
    if(ht_abs(x-rx)<=38 && ht_abs(y-ry)<=40) return HT_ROPE;
    if(ht_abs(x-t->boat_x/256)<=52 && ht_abs(y-ground)<=25) return HT_BOAT;
    if(ht_ladder_near(g)>=0) return HT_LADDER;
    return HT_FREE;
}
static bool ht_traversal_interact(void) {
    ht_traversal *t=&ht.traversal;
    if(t->mode==HT_LEDGE) {t->mode=HT_CLIMB;t->climb=0;return true;}
    if(t->mode==HT_CLIMB) return true;
    if(t->mode) { t->mode=HT_FREE; return true; }
    int near=ht_traversal_near(&ht);
    if(!near) return false;
    t->mode=(uint8_t)near;
    if(near==HT_LADDER) t->ladder=(uint8_t)ht_ladder_near(&ht);
    if(near==HT_ROPE) t->rope_awake=true;
    if(near==HT_ROLL || near==HT_CRATE) {
        int object=near==HT_ROLL?t->ball_x:t->crate_x;
        int reach=(near==HT_ROLL?53:25)*256;
        ht.x=object+(ht.x<object?-reach:reach);
    }
    if(near==HT_BOAT) t->ride_offset=ht_clamp(ht.x-t->boat_x,-30*256,30*256);
    ht.vx=ht.vy=0; return true;
}
/* Solid thin terraces permit travel beneath an upper route. Land/head/side
 * resolution uses previous position so standing under a deck cannot snap up. */
static void ht_collide(int left,int right,int top,int bottom,int old_x,int old_y) {
    const int radius=5*256,head=29*256;
    left*=256;right*=256;top*=256;bottom*=256;
    if(ht.x+radius<=left || ht.x-radius>=right) return;
    if(ht.vy>=0 && old_y<=top && ht.y>=top) {
        ht.y=top;ht.vy=0;ht.grounded=true;
    } else if(ht.vy<0 && old_y-head>=bottom && ht.y-head<bottom) {
        ht.y=bottom+head;ht.vy=0;
    } else if(ht.y>top+2*256 && ht.y-head<bottom) {
        if(old_x+radius<=left && ht.vx>0) {ht.x=left-radius;ht.vx=0;}
        else if(old_x-radius>=right && ht.vx<0) {ht.x=right+radius;ht.vx=0;}
    }
}
static void ht_step_controls(int direction,int vertical,bool jump_pressed,bool jump_held) {
    int start_x=ht.x,start_y=ht.y;
    ++ht.ticks;
    ht_traversal *t=&ht.traversal;
    int ground=ht_land[0].top;
    if(t->drop_cooldown) --t->drop_cooldown;
    if(t->mode==HT_LEDGE && vertical<0) {t->mode=HT_CLIMB;t->climb=0;}
    if(t->mode==HT_LEDGE && (vertical>0 || jump_pressed)) {
        t->mode=HT_FREE;t->drop_cooldown=20;ht.vy=0;ht.coyote=ht.jump_buffer=0;
        if(jump_pressed) {ht.vx=-t->ledge_side*640;ht.vy=-1200;}
        jump_pressed=false;
    }
    /* A released rolling object drops into the trench, creating real footing. */
    if(t->ball_phase==1) {
        t->ball_x=ht_min(390*256,t->ball_x+512);
        t->ball_y=ht_min((ground+48)*256,t->ball_y+768);
        if(t->ball_x==390*256 && t->ball_y==(ground+48)*256) t->ball_phase=2;
    }
    if(t->rope_awake) {
        t->rope_v=ht_clamp(t->rope_v-t->rope_x/128+(t->mode==HT_ROPE?direction*48:0),-1600,1600);
        t->rope_x+=t->rope_v;
        if(ht_abs(t->rope_x)>170*256) {
            t->rope_x=ht_clamp(t->rope_x,-170*256,170*256);t->rope_v=-t->rope_v/2;
        }
    }
    if(vertical && !t->mode) {
        int ladder=ht_ladder_near(&ht);
        if(ladder>=0) {t->mode=HT_LADDER;t->ladder=(uint8_t)ladder;}
    }
    if(jump_pressed && t->mode && t->mode!=HT_CLIMB) {
        int mode=t->mode;t->mode=HT_FREE;
        if(mode==HT_LADDER || mode==HT_ROPE || mode==HT_BOAT) {
            ht.vx=mode==HT_ROPE?ht_clamp(t->rope_v,-1000,1000):direction*640;
            ht.vy=-1880;ht.grounded=false;ht.coyote=0;jump_pressed=false;
        }
    }
    if(t->mode==HT_LEDGE || t->mode==HT_CLIMB) {
        const ht_platform *p=&ht_land[t->ledge];
        int edge=t->ledge_side>0?p->left:p->right;
        if(t->mode==HT_CLIMB && t->climb<16) ++t->climb;
        ht.x=(edge-t->ledge_side*6)*256+t->ledge_side*18*256*t->climb/16;
        ht.y=(p->top+27)*256-27*256*t->climb/16;
        ht.vx=ht.vy=0;ht.grounded=false;
        if(t->climb==16) {t->mode=HT_FREE;ht.grounded=true;}
    } else if(t->mode==HT_LADDER) {
        int top,bottom;ht_ladder_ends(ht.level,t->ladder,&top,&bottom);
        ht.x=ht_ladder_x(t->ladder)*256;
        ht.y=ht_clamp(ht.y+vertical*512,top*256,bottom*256);
        ht.vx=ht.vy=0;ht.grounded=false;
        if((ht.y==top*256 && vertical<0) || (ht.y==bottom*256 && vertical>0)) {
            t->mode=HT_FREE;ht.grounded=true;
        }
    } else if(t->mode==HT_ROPE) {
        int x,y;ht_rope_point(&ht,&x,&y);ht.x=x*256;ht.y=y*256;
        ht.vx=ht.vy=0;ht.grounded=false;
    } else if(t->mode==HT_BOAT) {
        t->boat_x=ht_clamp(t->boat_x+direction*384,1760*256,2160*256);
        ht.x=t->boat_x+t->ride_offset;ht.y=ground*256;
        ht.vx=ht.vy=0;ht.grounded=true;
    } else {
        if(jump_pressed) ht.jump_buffer=7;
        else if(ht.jump_buffer) --ht.jump_buffer;
        if(ht.grounded) ht.coyote=6;else if(ht.coyote) --ht.coyote;
        int target=direction*640;
        if(ht.vx<target) ht.vx=ht_min(target,ht.vx+100);
        if(ht.vx>target) ht.vx=ht_max(target,ht.vx-100);
        if(ht.jump_buffer && ht.coyote) {ht.vy=-1880;ht.grounded=false;ht.coyote=0;ht.jump_buffer=0;}
        if(!jump_held && ht.vy < -780) ht.vy=-780;
        int old_x=ht.x,old_y=ht.y;
        ht.vy=ht_min(ht.vy+88,2400);
        ht.x=ht_clamp(ht.x+ht.vx,6*256,(HT_GOAL+60)*256);ht.y+=ht.vy;ht.grounded=false;
        if(t->mode==HT_ROLL || t->mode==HT_CRATE) {
            int *object=t->mode==HT_ROLL?&t->ball_x:&t->crate_x;
            int half=t->mode==HT_ROLL?48:20;
            int side=old_x<*object?-1:1;
            *object=ht_clamp(ht.x-side*(half+5)*256,(t->mode==HT_ROLL?180:560)*256,(t->mode==HT_ROLL?350:735)*256);
            ht.x=*object+side*(half+5)*256;
            if(t->mode==HT_ROLL && *object==350*256) {t->ball_phase=1;t->mode=HT_FREE;ht.vx=0;}
        }
        for(int i=0;i<HT_PLATFORMS;++i) {
            const ht_platform *p=&ht_land[i];
            if(!t->mode && !t->drop_cooldown && !ht.grounded && direction &&
               ht.y>=(p->top+12)*256 && ht.y<=(p->top+32)*256 &&
               ht_abs(ht.x/256-(direction>0?p->left:p->right))<=9 &&
               (direction>0?old_x<p->left*256:old_x>p->right*256)) {
                t->mode=HT_LEDGE;t->ledge=(uint8_t)i;t->ledge_side=direction;t->climb=0;
                ht.x=((direction>0?p->left:p->right)-direction*6)*256;
                ht.y=(p->top+27)*256;ht.vx=ht.vy=0;ht.coyote=ht.jump_buffer=0;
                break;
            }
            ht_collide(p->left,p->right,p->top,(i==0 || i==1 || i==5 || i==6 || i==9)?400:p->top+18,old_x,old_y);
        }
        if(t->ball_phase!=1 && t->mode!=HT_ROLL) ht_collide(t->ball_x/256-48,t->ball_x/256+48,t->ball_y/256-48,t->ball_y/256+48,old_x,old_y);
        if(t->mode!=HT_CRATE) ht_collide(t->crate_x/256-20,t->crate_x/256+20,ground-64,ground,old_x,old_y);
        ht_collide(t->boat_x/256-44,t->boat_x/256+44,ground,ground+12,old_x,old_y);
    }
    if((direction || vertical || t->mode==HT_CLIMB) && (start_x!=ht.x || start_y!=ht.y)) ht.sway_phase+=3;
    if(direction) ht.facing=direction;
    if(ht.grounded) {
        ht.stride+=ht_abs(ht.vx);
        for(int i=3;i<=9;i+=3) if(ht.x/256>=ht_land[i].left && ht.x/256<ht_land[i].right && ht.y==ht_land[i].top*256) ht.checkpoint=i;
    }
    if((!ht.puzzle.solved || ht_final_locked(&ht)) && ht.x>HT_PUZZLE_GATE*256) {ht.x=HT_PUZZLE_GATE*256;ht.vx=0;}
    ht.story_x=ht_max(ht.story_x,ht.x/256);
    unsigned beat=ht_story_beat(ht.story_x)+1;
    if(ht.story_seen[ht.level]<beat) ht.story_seen[ht.level]=(uint8_t)beat;
    if(ht.y>335*256 || (ht.x>1790*256 && ht.x<2110*256 && ht.y>(ground+12)*256)) {
        ++ht.deaths;ht_spawn(false);return;
    }
    if(ht.x>=HT_GOAL*256 && ht.grounded && ht.puzzle.solved && !ht_final_locked(&ht)) {
        if(++ht.level==HT_LEVELS) {ht.level=0;++ht.laps;} ht_spawn(true);return;
    }
    ht.camera+=(ht_clamp(ht.x/256-165,0,HT_GOAL-340)*256-ht.camera)/8;
    ht.camera_y+=(ht_clamp(ht.y/256-180,-300,100)*256-ht.camera_y)/8;
}
/* Keep the simple horizontal input entry point for SDK examples and tests. */
static void ht_step(int direction,bool jump_pressed,bool jump_held) { ht_step_controls(direction,0,jump_pressed,jump_held); }
