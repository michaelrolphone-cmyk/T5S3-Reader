/* Bounded fixed-point rigid bodies and a seven-node constrained rope. */
static const ht_mechanics *ht_mech(const ht_game *g) {return &ht_mechanics_by_level[g->level];}
static int ht_ladder_x(unsigned i) {return ht_mech(&ht)->ladders[i].x;}
static void ht_ladder_ends(unsigned level,unsigned i,int *top,int *bottom) {
    const ht_mechanics *m=&ht_mechanics_by_level[level];
    *top=ht_level_land(level)[m->ladders[i].upper].top;
    *bottom=ht_level_land(level)[m->ladders[i].lower].top;
}
static int ht_ladder_near(const ht_game *g) {
    const ht_mechanics *m=ht_mech(g);
    for(unsigned i=0;i<3;++i) if(m->ladders[i].x) {
        int top,bottom;ht_ladder_ends(g->level,i,&top,&bottom);
        if(ht_abs(g->x/256-m->ladders[i].x)<=12 && g->y/256>=top-3 && g->y/256<=bottom+3) return (int)i;
    }
    return -1;
}
static int ht_boat_top(const ht_game *g) {
    int phase=(int)(g->ticks&63u),wave=phase<32?phase:64-phase;
    return ht_mech(g)->boat_deck*256+(wave-32)*12;
}
static void ht_rope_point(const ht_game *g,int *x,int *feet) {
    *x=g->traversal.rope_px[HT_ROPE_NODES-1]/256;
    *feet=g->traversal.rope_py[HT_ROPE_NODES-1]/256+24;
}
static bool ht_solid_platform(const ht_game *g,int i) {
    return i!=ht_mech(g)->bridge || g->traversal.bridge_open==32;
}
static int ht_traversal_near(const ht_game *g) {
    const ht_traversal *t=&g->traversal;const ht_mechanics *m=ht_mech(g);
    if(t->mode) return t->mode;
    int x=g->x/256,y=g->y/256;
    if(m->ball_x && g->grounded && ht_abs(x-t->ball_x/256)<=HT_BALL_RADIUS+9 &&
       ht_abs(y-(t->ball_y/256+HT_BALL_RADIUS))<=5) return HT_ROLL;
    if(m->crate_x && g->grounded && ht_abs(x-t->crate_x/256)<=HT_CRATE_HALF+9 &&
       ht_abs(y-t->crate_y/256)<=5) return HT_CRATE;
    int rx,ry;ht_rope_point(g,&rx,&ry);
    if(m->rope_length && ht_abs(x-rx)<=12 && ht_abs(y-ry)<=12) return HT_ROPE;
    if(m->boat_right && ht_abs(x-t->boat_x/256)<=36 && ht_abs(g->y-ht_boat_top(g))<=16*256) return HT_BOAT;
    if(ht_ladder_near(g)>=0) return HT_LADDER;
    return HT_FREE;
}
static bool ht_traversal_interact(void) {
    ht_traversal *t=&ht.traversal;
    if(t->mode==HT_LEDGE) {t->mode=HT_CLIMB;t->climb=0;return true;}
    if(t->mode==HT_CLIMB) return true;
    if(t->mode) {
        if(t->mode==HT_ROPE) {
            ht.vx=t->rope_px[6]-t->rope_ox[6];ht.vy=t->rope_py[6]-t->rope_oy[6];
        } else if(t->mode==HT_BOAT) ht.vx=t->boat_vx;
        t->mode=HT_FREE;return true;
    }
    int near=ht_traversal_near(&ht);if(!near) return false;
    t->mode=(uint8_t)near;t->fall_ticks=0;
    if(near==HT_LADDER) t->ladder=(uint8_t)ht_ladder_near(&ht);
    if(near==HT_ROPE) {
        /* Keep the caught hand position: no teleport to an authored arc. */
        t->rope_px[6]=ht.x;t->rope_py[6]=ht.y-24*256;
        t->rope_ox[6]=ht.x-ht.vx;t->rope_oy[6]=t->rope_py[6]-ht.vy;
        t->rope_awake=true;
    }
    if(near==HT_BOAT) t->ride_offset=ht_clamp(ht.x-t->boat_x,-22*256,22*256);
    ht.vx=ht.vy=0;return true;
}
static int ht_drag(int velocity,int amount) {
    return velocity>0?ht_max(0,velocity-amount):ht_min(0,velocity+amount);
}
static bool ht_plate_weighted(const ht_game *g) {
    const ht_mechanics *m=ht_mech(g);const ht_traversal *t=&g->traversal;
    if(!m->plate_kind) return false;
    int x=m->plate_kind==1?t->ball_x:t->crate_x;
    int bottom=m->plate_kind==1?t->ball_y+HT_BALL_RADIUS*256:t->crate_y;
    return ht_abs(x-m->plate_x*256)<=20*256 && ht_abs(bottom-m->plate_y*256)<=2*256;
}
/* Split the authored stone socket out of the floor. Every collider and the
 * artwork use these same segments; filling a socket is a physical action. */
static int ht_platform_piece(const ht_game *g,int i,int part,int *left,int *right,int *top) {
    const ht_platform *p=&ht_level_land(g->level)[i];const ht_mechanics *m=ht_mech(g);
    *left=p->left;*right=p->right;*top=p->top;
    if(i!=m->pit_platform) return part==0;
    if(part==0) *right=m->pit_x-(HT_BALL_RADIUS+8);
    else if(part==1) *left=m->pit_x+(HT_BALL_RADIUS+8);
    else if(part==2) {*left=m->pit_x-(HT_BALL_RADIUS+8);*right=m->pit_x+(HT_BALL_RADIUS+8);*top=m->pit_floor;}
    else return false;
    return true;
}
static void ht_body_step(int *x,int *y,int *vx,int *vy,int radius,int height,bool round) {
    int old_x=*x,old_y=*y;*vy=ht_min(*vy+88,2400);*x+=*vx;*y+=*vy;
    for(int i=0;i<HT_PLATFORMS;++i) if(ht_solid_platform(&ht,i)) for(int part=0;part<3;++part) {
        int l,r,top;if(!ht_platform_piece(&ht,i,part,&l,&r,&top)) break;
        int dx=ht_max(l-*x/256,ht_max(*x/256-r,0));
        if(dx>radius) continue;
        int rest=(top-(round?(int)ht_isqrt((unsigned)(radius*radius-dx*dx)):0))*256;
        if(*vy>=0 && old_y<=rest+256 && *y>=rest) {*y=rest;*vy=0;}
        int bottom=round?*y+radius*256:*y,head=round?*y-radius*256:*y-height*256;
        int wall_bottom=(i==0 || i==9)?500:top+18;
        if(bottom>top*256+256 && head<wall_bottom*256) {
            if(old_x+radius*256<=l*256 && *vx>0) {*x=(l-radius)*256;*vx=0;}
            else if(old_x-radius*256>=r*256 && *vx<0) {*x=(r+radius)*256;*vx=0;}
        }
    }
}
static void ht_props_step(int direction) {
    const ht_mechanics *m=ht_mech(&ht);ht_traversal *t=&ht.traversal;
    int ball_old=t->ball_x,crate_old=t->crate_x,boat_old=t->boat_x;
    if(m->ball_x) {
        t->ball_vx=ht_clamp(ht_drag(t->ball_vx,4)+(t->mode==HT_ROLL?direction*14:0),-224,224);
        ht_body_step(&t->ball_x,&t->ball_y,&t->ball_vx,&t->ball_vy,HT_BALL_RADIUS,0,true);
        t->ball_roll+=t->ball_x-ball_old;
        t->ball_phase=t->ball_vy?1:2;
        if(t->ball_y>500*256) {t->ball_x=m->ball_x*256;t->ball_y=(m->ball_floor-HT_BALL_RADIUS)*256;t->ball_vx=t->ball_vy=0;}
    }
    if(m->crate_x) {
        t->crate_vx=ht_clamp(ht_drag(t->crate_vx,24)+(t->mode==HT_CRATE?direction*36:0),-192,192);
        ht_body_step(&t->crate_x,&t->crate_y,&t->crate_vx,&t->crate_vy,HT_CRATE_HALF,HT_CRATE_HEIGHT,false);
        if(t->crate_y>500*256) {t->crate_x=m->crate_x*256;t->crate_y=m->crate_floor*256;t->crate_vx=t->crate_vy=0;}
    }
    if(m->boat_right) {
        t->boat_vx=ht_clamp(ht_drag(t->boat_vx,3)+(t->mode==HT_BOAT?direction*15:0),-320,320);
        t->boat_x=ht_clamp(t->boat_x+t->boat_vx,m->boat_left*256,m->boat_right*256);
        if(t->boat_x==m->boat_left*256 || t->boat_x==m->boat_right*256) t->boat_vx=0;
    }
    if(t->support==1) ht.x+=t->ball_x-ball_old;
    if(t->support==2) ht.x+=t->crate_x-crate_old;
    if(t->support==3) ht.x+=t->boat_x-boat_old;
    if(t->bridge_open<32) {
        if(ht_plate_weighted(&ht)) ++t->bridge_open;
        else if(t->bridge_open) --t->bridge_open;
    }
    /* Verlet inertia, gravity, hand pumping and six length-constraint passes.
     * Seven nodes / 36 segment solves is a fixed simulation budget. */
    if(m->rope_length) {
        for(int i=1;i<HT_ROPE_NODES;++i) {
            int x=t->rope_px[i],y=t->rope_py[i];
            t->rope_px[i]+=ht_drag(x-t->rope_ox[i],1)+(i==6 && t->mode==HT_ROPE?direction*384:0);
            t->rope_py[i]+=ht_drag(y-t->rope_oy[i],1)+88;
            t->rope_ox[i]=x;t->rope_oy[i]=y;
        }
        int length=m->rope_length*256/(HT_ROPE_NODES-1);
        for(int pass=0;pass<6;++pass) {
            t->rope_px[0]=m->rope_anchor_x*256;t->rope_py[0]=m->rope_anchor_y*256;
            for(int i=1;i<HT_ROPE_NODES;++i) {
                int dx=t->rope_px[i]-t->rope_px[i-1],dy=t->rope_py[i]-t->rope_py[i-1];
                int sx=dx,sy=dy;
                while(ht_abs(sx)>32767 || ht_abs(sy)>32767) {sx/=2;sy/=2;}
                int distance=(int)ht_isqrt((unsigned)(sx*sx+sy*sy));
                if(!distance) continue;
                /* Project the scaled direction; products stay within int32,
                 * without an unavailable 64-bit divide runtime import. */
                int cx=dx-sx*length/distance,cy=dy-sy*length/distance;
                if(i==1) {t->rope_px[i]-=cx;t->rope_py[i]-=cy;}
                else {t->rope_px[i-1]+=cx/2;t->rope_py[i-1]+=cy/2;t->rope_px[i]-=cx-cx/2;t->rope_py[i]-=cy-cy/2;}
            }
        }
        t->rope_x=t->rope_px[6]-m->rope_anchor_x*256;t->rope_v=t->rope_px[6]-t->rope_ox[6];
    }
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
        if(old_x+radius<=left && ht.x>old_x) {ht.x=left-radius;ht.vx=0;}
        else if(old_x-radius>=right && ht.x<old_x) {ht.x=right+radius;ht.vx=0;}
    }
}
static void ht_step_controls(int direction,int vertical,bool jump_pressed,bool jump_held) {
    int start_x=ht.x,start_y=ht.y,fall_speed=ht.vy;
    bool was_grounded=ht.grounded;
    if(ht.observation_ticks) --ht.observation_ticks;
    if(ht.landing_ticks) --ht.landing_ticks;
    ++ht.ticks;
    int wind=ht_wind(&ht);
    ht.weather_drift=(ht.weather_drift+wind)%(HT_W*256);
    ht_traversal *t=&ht.traversal;
    const ht_mechanics *m=ht_mech(&ht);
    ht_props_step(direction);
    uint8_t old_support=t->support;t->support=0;
    if(t->drop_cooldown) --t->drop_cooldown;
    if(t->mode==HT_LEDGE && vertical<0) {t->mode=HT_CLIMB;t->climb=0;}
    if(t->mode==HT_LEDGE && (vertical>0 || jump_pressed)) {
        t->mode=HT_FREE;t->drop_cooldown=20;ht.vy=0;ht.coyote=ht.jump_buffer=0;
        if(jump_pressed) {ht.vx=-t->ledge_side*640;ht.vy=-1200;}
        jump_pressed=false;
    }
    if(jump_pressed && t->mode && t->mode!=HT_CLIMB) {
        int mode=t->mode;t->mode=HT_FREE;
        if(mode==HT_LADDER || mode==HT_ROPE || mode==HT_BOAT) {
            ht.vx=mode==HT_ROPE?ht_clamp(t->rope_px[6]-t->rope_ox[6],-1000,1000):direction*640;
            ht.vy=mode==HT_ROPE?ht_max(-1880,ht_min(0,t->rope_py[6]-t->rope_oy[6])-1200):-1880;ht.grounded=false;ht.coyote=0;jump_pressed=false;
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
        int old_x=ht.x,old_y=ht.y;
        ht.x=t->rope_px[6];ht.y=t->rope_py[6]+24*256;
        ht.vx=ht.x-old_x;ht.vy=ht.y-old_y;ht.grounded=false;
        for(int i=0;i<HT_PLATFORMS;++i) if(ht_solid_platform(&ht,i))
            ht_collide(ht_land[i].left,ht_land[i].right,ht_land[i].top,ht_land[i].top+18,old_x,old_y);
        t->rope_px[6]=ht.x;t->rope_py[6]=ht.y-24*256;
    } else if(t->mode==HT_BOAT) {
        ht.x=t->boat_x+t->ride_offset;ht.y=ht_boat_top(&ht);
        ht.vx=t->boat_vx;ht.vy=0;ht.grounded=true;t->support=3;
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
        ht.x=ht_clamp(ht.x+ht.vx+(t->mode==HT_FREE?wind:0),6*256,(HT_GOAL+60)*256);ht.y+=ht.vy;ht.grounded=false;
        if(t->mode==HT_ROLL || t->mode==HT_CRATE) {
            int object=t->mode==HT_ROLL?t->ball_x:t->crate_x;
            int bottom=t->mode==HT_ROLL?t->ball_y+HT_BALL_RADIUS*256:t->crate_y;
            int half=t->mode==HT_ROLL?HT_BALL_RADIUS:HT_CRATE_HALF;
            int side=old_x<object?-1:1;
            ht.x=object+side*(half+5)*256;
            ht.vx=t->mode==HT_ROLL?t->ball_vx:t->crate_vx;
            if(ht_abs(bottom-old_y)>6*256) t->mode=HT_FREE;
        }
        if(!ht.grounded && ht.vy>0 && !t->mode) {
            if(++t->fall_ticks*HT_STEP_MS>1000u) {++ht.deaths;ht_spawn(false);return;}
        }
        for(int i=0;i<HT_PLATFORMS;++i) {
            if(!ht_solid_platform(&ht,i)) continue;
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
            for(int part=0;part<3;++part) {
                int l,r,top;if(!ht_platform_piece(&ht,i,part,&l,&r,&top)) break;
                ht_collide(l,r,top,(i==0 || i==9)?500:top+18,old_x,old_y);
            }
        }
        if(m->ball_x && t->mode!=HT_ROLL) {
            int dx=(ht.x-t->ball_x)/256;
            if(ht_abs(dx)<HT_BALL_RADIUS) {
                int surface=t->ball_y-(int)ht_isqrt((unsigned)(HT_BALL_RADIUS*HT_BALL_RADIUS-dx*dx))*256;
                if(ht.vy>=0 && (old_y<=surface+256 || (old_support==1 && ht_abs(old_y-surface)<6*256)) && ht.y>=surface) {
                    ht.y=surface;ht.vy=0;ht.grounded=true;t->support=1;
                }
            }
            if(t->support!=1) {
                int closest=ht_clamp(t->ball_y,ht.y-29*256,ht.y),dy=(closest-t->ball_y)/256;
                if(ht_abs(dy)<HT_BALL_RADIUS) {
                    int reach=(int)ht_isqrt((unsigned)(HT_BALL_RADIUS*HT_BALL_RADIUS-dy*dy))+5;
                    if(ht_abs(ht.x-t->ball_x)<reach*256) {ht.x=t->ball_x+(old_x<t->ball_x?-reach:reach)*256;ht.vx=0;}
                }
            }
        }
        if(m->crate_x && t->mode!=HT_CRATE) {
            int top=t->crate_y/256-HT_CRATE_HEIGHT;
            ht_collide(t->crate_x/256-HT_CRATE_HALF,t->crate_x/256+HT_CRATE_HALF,top,t->crate_y/256,old_x,old_y);
            if(ht.grounded && ht.y==top*256 && ht_abs(ht.x-t->crate_x)<(HT_CRATE_HALF+5)*256) t->support=2;
        }
        if(m->boat_right) {
            int top=ht_boat_top(&ht)/256;
            if(old_support==3 && ht.vy>=0 && ht_abs(ht.x-t->boat_x)<29*256 && ht_abs(old_y-ht_boat_top(&ht))<3*256) {
                ht.y=top*256;ht.vy=0;ht.grounded=true;
            } else ht_collide(t->boat_x/256-29,t->boat_x/256+29,top,top+10,old_x,old_y);
            if(ht.grounded && ht.y==top*256 && ht_abs(ht.x-t->boat_x)<34*256) t->support=3;
        }
    }
    if((direction || vertical || t->mode==HT_CLIMB) && (start_x!=ht.x || start_y!=ht.y)) ht.sway_phase+=5;
    if(ht.grounded && !was_grounded && fall_speed>384 && !t->mode) {
        ht.landing_ticks=10;ht.landing_strength=(uint8_t)ht_clamp(fall_speed/256,2,8);
        ht.landing_x=ht.x/256;ht.landing_y=ht.y/256;
    }
    if(ht.grounded || t->mode) t->fall_ticks=0;
    if(direction) ht.facing=direction;
    if(ht.grounded) {
        ht.stride+=ht_abs(ht.vx);
        for(int i=3;i<=9;i+=3) if(ht.x/256>=ht_land[i].left && ht.x/256<ht_land[i].right && ht.y==ht_land[i].top*256) ht.checkpoint=i;
    }
    if((!ht.puzzle.solved || ht_final_locked(&ht)) && ht.x>HT_PUZZLE_GATE*256) {ht.x=HT_PUZZLE_GATE*256;ht.vx=0;}
    ht.story_x=ht_max(ht.story_x,ht.x/256);
    unsigned beat=ht_story_beat(ht.story_x)+1;
    if(ht.story_seen[ht.level]<beat) ht.story_seen[ht.level]=(uint8_t)beat;
    if(ht.y>500*256 || (m->water_right && ht.x>m->water_left*256 && ht.x<m->water_right*256 && ht.y>(m->boat_deck+18)*256)) {
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
