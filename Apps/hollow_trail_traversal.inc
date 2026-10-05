/* Bounded fixed-point rigid bodies and a seven-node constrained rope. */
static const ht_mechanics *ht_mech(const ht_game *g) {return &ht_mechanics_by_level[g->level];}
static int ht_ladder_x(unsigned i) {return ht_mech(&ht)->ladders[i].x;}
static void ht_ladder_ends(unsigned level,unsigned i,int *top,int *bottom) {
    const ht_mechanics *m=&ht_mechanics_by_level[level];
    *top=ht_land_height(level,m->ladders[i].upper,m->ladders[i].x);
    *bottom=ht_land_height(level,m->ladders[i].lower,m->ladders[i].x);
}
static int ht_ladder_near(const ht_game *g) {
    const ht_mechanics *m=ht_mech(g);
    for(unsigned i=0;i<3;++i) if(m->ladders[i].x) {
        int top,bottom;ht_ladder_ends(g->level,i,&top,&bottom);
        if(ht_abs(g->x/256-m->ladders[i].x)<=12 && g->y/256>=top-3 && g->y/256<=bottom+24) return (int)i;
    }
    return -1;
}
static bool ht_ladder_enter(int vertical) {
    ht_traversal *t=&ht.traversal;
    if(!vertical || t->mode || t->drop_cooldown) return false;
    int ladder=ht_ladder_near(&ht);if(ladder<0) return false;
    int top,bottom;ht_ladder_ends(ht.level,(unsigned)ladder,&top,&bottom);
    /* Outward input at either end must not pin the player to the ladder. */
    if((vertical<0 && ht.y<=top*256) || (vertical>0 && ht.y>=bottom*256)) return false;
    t->mode=HT_LADDER;t->ladder=(uint8_t)ladder;
    ht.x=ht_ladder_x((unsigned)ladder)*256;
    ht.vx=ht.vy=0;ht.grounded=false;ht.coyote=ht.jump_buffer=0;
    return true;
}
/* Up is deliberate climbing intent. Sideways jumps and A never attach. */
static bool ht_tree_enter(int direction,int vertical,bool jumping) {
    ht_traversal *t=&ht.traversal;
    if(vertical>=0 || direction || jumping || t->mode || t->drop_cooldown)return false;
    int i=ht_tree_near(&ht);if(i<0)return false;
    ht_climb_tree tree;if(!ht_existing_tree(&ht,i,&tree))return false;
    int left=ht_tree_edge(&tree,ht.y/256,-1),right=ht_tree_edge(&tree,ht.y/256,1);
    t->tree=(uint8_t)i;t->tree_side=ht_abs(ht.x/256-left)<ht_abs(ht.x/256-right)?-1:1;
    t->mode=HT_TREE;ht.x=(t->tree_side<0?left:right)*256;
    ht.vx=ht.vy=0;ht.grounded=false;ht.coyote=ht.jump_buffer=0;return true;
}
static void ht_tree_collision(int old_x,int old_y,unsigned old_support) {
    if(ht.traversal.branch_drop || ht.vy<0)return;
    for(int i=0;i<=HT_PLATFORMS;++i) {
        ht_climb_tree tree;if(!ht_existing_tree(&ht,i,&tree) || ht_abs(ht.x/256-tree.x)>tree.height/2)continue;
        for(int b=0;b<6;++b) {
            int top,old_top;
            if(!ht_tree_footing(&ht,&tree,b,ht.x/256,&top) || !ht_tree_footing(&ht,&tree,b,old_x/256,&old_top))continue;
            bool follows=old_support==5 && ht.vy<=HT_GRAVITY && ht_abs(old_y-old_top*256)<=256;
            if((old_y<=old_top*256+256 && ht.y>=top*256) || follows) {
                if(ht.grounded && ht.y<top*256)continue;
                ht.y=top*256;ht.vy=0;ht.grounded=true;ht.traversal.support=5;
            }
        }
    }
}
static int ht_boat_half(const ht_game *g) {return g->level==4?48:HT_BOAT_HALF;}
static int ht_boat_top(const ht_game *g) {
    int phase=(int)(g->ticks&63u),wave=phase<32?phase:64-phase;
    return ht_mech(g)->boat_deck*256+(wave-32)*12;
}
static int ht_boat_stroke(const ht_game *g) {
    if(g->traversal.mode!=HT_BOAT || ht_abs(g->traversal.boat_vx)<=32) return 0;
    int phase=(int)((g->ticks/2)%32),triangle=phase<16?phase:32-phase;
    return triangle-8;
}
static int ht_rope_segment(const ht_game *g,int *fraction) {
    int along=ht_clamp(g->traversal.rope_grip,0,ht_mech(g)->rope_length*256);
    int coordinate=along*(HT_ROPE_NODES-1)/ht_max(1,ht_mech(g)->rope_length);
    int node=ht_min(HT_ROPE_NODES-2,coordinate/256);
    *fraction=coordinate-node*256;return node;
}
static void ht_rope_grip_point(const ht_game *g,int *x,int *y,bool previous) {
    int fraction,node=ht_rope_segment(g,&fraction);
    const int *px=previous?g->traversal.rope_ox:g->traversal.rope_px;
    const int *py=previous?g->traversal.rope_oy:g->traversal.rope_py;
    *x=px[node]+(px[node+1]-px[node])*fraction/256;
    *y=py[node]+(py[node+1]-py[node])*fraction/256;
}
static void ht_rope_velocity(const ht_game *g,int *vx,int *vy) {
    int px,py,ox,oy,fraction,node=ht_rope_segment(g,&fraction);
    ht_rope_grip_point(g,&px,&py,false);ht_rope_grip_point(g,&ox,&oy,true);
    int dx=(g->traversal.rope_px[node+1]-g->traversal.rope_px[node])/256;
    int dy=(g->traversal.rope_py[node+1]-g->traversal.rope_py[node])/256;
    int length=ht_max(1,(int)ht_isqrt((unsigned)(dx*dx+dy*dy)));
    *vx=px-ox+dx*g->traversal.rope_slide_v/length;
    *vy=py-oy+dy*g->traversal.rope_slide_v/length;
}
static void ht_rope_point(const ht_game *g,int *x,int *feet) {
    int px,py;ht_rope_grip_point(g,&px,&py,false);*x=px/256;*feet=py/256+24;
}
/* Nearest point on any segment within the character's reach. This is a
 * geometric grab, not an automatic attachment or a tail-only trigger. */
static int ht_rope_catch(const ht_game *g) {
    if(!ht_mech(g)->rope_length) return -1;
    int best=1400,grip=-1,hx=g->x/256,hy=g->y/256-24;
    for(int i=0;i<HT_ROPE_NODES-1;++i) {
        int ax=g->traversal.rope_px[i]/256,ay=g->traversal.rope_py[i]/256;
        int dx=g->traversal.rope_px[i+1]/256-ax,dy=g->traversal.rope_py[i+1]/256-ay;
        int length=dx*dx+dy*dy;
        int f=length?ht_clamp(((hx-ax)*dx+(hy-ay)*dy)*256/length,0,256):0;
        int rx=ax+dx*f/256,ry=ay+dy*f/256;
        int distance=(hx-rx)*(hx-rx)+(hy-ry)*(hy-ry);
        if(distance<best && ht_abs(hx-rx)<=12 && ry>=hy-12 && ry<=hy+24) {best=distance;grip=(i*256+f)*ht_mech(g)->rope_length/(HT_ROPE_NODES-1);}
    }
    return grip;
}
static int ht_traversal_near(const ht_game *g) {
    const ht_traversal *t=&g->traversal;const ht_mechanics *m=ht_mech(g);
    if(t->mode) return t->mode;
    int x=g->x/256,y=g->y/256;
    if(m->ball_x && g->grounded && ht_abs(x-t->ball_x/256)<=HT_BALL_RADIUS+9 &&
       ht_abs(y-(t->ball_y/256+HT_BALL_RADIUS))<=5) return HT_ROLL;
    if(m->crate_x && g->grounded && ht_abs(x-t->crate_x/256)<=HT_CRATE_HALF+9 &&
       ht_abs(y-t->crate_y/256)<=5) return HT_CRATE;
    if(ht_rope_catch(g)>=0) return HT_ROPE;
    if(m->boat_right && ht_abs(x-t->boat_x/256)<=ht_boat_half(g)+6 && ht_abs(g->y-ht_boat_top(g))<=16*256) return HT_BOAT;
    if(ht_ladder_near(g)>=0) return HT_LADDER;
    return HT_FREE;
}
static bool ht_forest_log_near(const ht_game *g) {
    return g->level==0 && !g->traversal.forest_log_phase && !g->traversal.forest_log_falling &&
        g->grounded && g->x/256>=342 && g->x/256<=366 && ht_abs(g->y/256-220)<=10;
}
static bool ht_traversal_interact(void) {
    ht_traversal *t=&ht.traversal;
    if(!t->mode && ht_forest_log_near(&ht)) {
        t->mode=HT_PUSH;t->forest_push=0;ht.vx=ht.vy=0;return true;
    }
    if(t->mode==HT_TREE)return false; /* A remains inspection, never tree capture/release. */
    if(t->mode==HT_LEDGE) {t->mode=HT_CLIMB;t->climb=t->climb_phase=0;return true;}
    if(t->mode==HT_CLIMB) return true;
    if(t->mode) {
        if(t->mode==HT_ROPE) {
            ht_rope_velocity(&ht,&ht.vx,&ht.vy);
        } else if(t->mode==HT_BOAT) ht.vx=t->boat_vx;
        t->mode=HT_FREE;return true;
    }
    int near=ht_traversal_near(&ht);if(!near || near==HT_LADDER) return false;
    t->mode=(uint8_t)near;
    if(near==HT_ROPE) {
        t->rope_grip=ht_rope_catch(&ht);t->rope_slide_v=0;
        int px,py;ht_rope_grip_point(&ht,&px,&py,false);
        ht.x=px;ht.y=py+24*256;t->rope_awake=true;ht.grounded=false;
    }
    if(near==HT_BOAT) {t->ride_offset=0;ht.x=t->boat_x;ht.y=ht_boat_top(&ht);}
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
    *left=p->left;*right=p->right;*top=ht_platform_top(g,i);
    if(g->level==0 && i==1)return part==0 && g->traversal.forest_log_phase==32;
    if(i!=m->pit_platform) return part==0;
    if(part==0) *right=m->pit_x-(HT_BALL_RADIUS+8);
    else if(part==1) *left=m->pit_x+(HT_BALL_RADIUS+8);
    else if(part==2) {*left=m->pit_x-(HT_BALL_RADIUS+8);*right=m->pit_x+(HT_BALL_RADIUS+8);*top=m->pit_floor;}
    else return false;
    return true;
}
static void ht_body_step(int *x,int *y,int *vx,int *vy,int radius,int height,bool round) {
    int old_x=*x,old_y=*y;*vy=ht_min(*vy+88,2400);*x+=*vx;*y+=*vy;
    for(int i=0;i<HT_PLATFORMS;++i) for(int part=0;part<3;++part) {
        int l,r,top;if(!ht_platform_piece(&ht,i,part,&l,&r,&top)) break;
        int dx=ht_max(l-*x/256,ht_max(*x/256-r,0));
        int old_top=part==2?top:ht_surface_at(&ht,i,ht_clamp(old_x/256,l,r));
        if(part!=2) top=ht_surface_at(&ht,i,ht_clamp(*x/256,l,r));
        if(dx>radius) continue;
        int rest=(top-(round?(int)ht_isqrt((unsigned)(radius*radius-dx*dx)):0))*256;
        bool follows=*vy==88 && old_x>=l*256 && old_x<r*256 && *x>=l*256 && *x<r*256 &&
            ht_abs(old_y-(old_top-(round?radius:0))*256)<=256;
        if(*vy>=0 && ((old_y<=rest+(old_top-top)*256+256 && *y>=rest) || follows)) {*y=rest;*vy=0;}
        int bottom=round?*y+radius*256:*y,head=round?*y-radius*256:*y-height*256;
        int wall_bottom=ht_terrain_bottom(&ht,i,*x/256);
        if(bottom>top*256+256 && head<wall_bottom*256) {
            int old_head=round?old_y-radius*256:old_y-height*256;
            if(*x>=l*256 && *x<r*256 && old_head>=ht_terrain_bottom(&ht,i,old_x/256)*256) {*x=old_x;*vx=0;}
            int wall_l=l,wall_r=r,old_l=l,old_r=r;
            if(part==0 && head/256>top+8) {
                ht_cliff_bounds(&ht,i,head/256,bottom/256,&wall_l,&wall_r);
                int old_bottom=round?old_y+radius*256:old_y;
                ht_cliff_bounds(&ht,i,old_head/256,old_bottom/256,&old_l,&old_r);
            }
            if(old_x+radius*256<=old_l*256 && *x+radius*256>wall_l*256) {*x=(wall_l-radius)*256;*vx=0;}
            else if(old_x-radius*256>=old_r*256 && *x-radius*256<wall_r*256) {*x=(wall_r+radius)*256;*vx=0;}
        }
    }
}
static void ht_props_step(int direction) {
    const ht_mechanics *m=ht_mech(&ht);ht_traversal *t=&ht.traversal;
    int ball_old=t->ball_x,crate_old=t->crate_x,boat_old=t->boat_x;
    int bridge_old=t->bridge_open;
    if(m->ball_x) {
        t->ball_vx=ht_clamp(ht_drag(t->ball_vx,4)+(t->mode==HT_ROLL?direction*ht_emphasis_amount(14):0),-224,224);
        ht_body_step(&t->ball_x,&t->ball_y,&t->ball_vx,&t->ball_vy,HT_BALL_RADIUS,0,true);
        t->ball_roll+=t->ball_x-ball_old;
        t->ball_phase=t->ball_vy?1:2;
        if(t->ball_y>500*256) {t->ball_x=m->ball_x*256;t->ball_y=(m->ball_floor-HT_BALL_RADIUS)*256;t->ball_vx=t->ball_vy=0;}
    }
    if(m->crate_x) {
        t->crate_vx=ht_clamp(ht_drag(t->crate_vx,24)+(t->mode==HT_CRATE?direction*ht_emphasis_amount(36):0),-192,192);
        ht_body_step(&t->crate_x,&t->crate_y,&t->crate_vx,&t->crate_vy,HT_CRATE_HALF,HT_CRATE_HEIGHT,false);
        if(t->crate_y>500*256) {t->crate_x=m->crate_x*256;t->crate_y=m->crate_floor*256;t->crate_vx=t->crate_vy=0;}
    }
    if(m->boat_right) {
        t->boat_vx=ht_clamp(ht_drag(t->boat_vx,3)+(t->mode==HT_BOAT?direction*ht_emphasis_amount(15):0),-320,320);
        int left=ht_max(m->boat_left,m->water_left+ht_boat_half(&ht))*256;
        int right=ht_min(m->boat_right,m->water_right-ht_boat_half(&ht))*256;
        t->boat_x=ht_clamp(t->boat_x+t->boat_vx,left,right);
        if(t->boat_x==left || t->boat_x==right) t->boat_vx=0;
    }
    if(t->support==1) ht.x+=t->ball_x-ball_old;
    if(t->support==2) ht.x+=t->crate_x-crate_old;
    if(t->support==3) ht.x+=t->boat_x-boat_old;
    if(ht.level==0 && t->forest_log_falling && t->forest_log_phase<32) {
        /* Gravity torque grows with lean; integrate angular speed rather
         * than advancing a fixed animation phase. */
        t->forest_log_speed+=(uint16_t)(1+ht_forest_log_sine(t->forest_log_angle)*22/256);
        t->forest_log_angle=(uint16_t)ht_min(4096,t->forest_log_angle+t->forest_log_speed);
        t->forest_log_phase=(uint8_t)(t->forest_log_angle==4096?32:ht_min(31,t->forest_log_angle*32/4096));
        if(t->forest_log_phase==32) {t->bridge_open=32;t->forest_log_falling=false;}
    }
    if(ht.level!=0 && t->bridge_open<32) {
        if(ht_plate_weighted(&ht)) ++t->bridge_open;
        else if(t->bridge_open) --t->bridge_open;
    }
    if(ht.level!=0 && t->support==4) ht.y+=(t->bridge_open-bridge_old)*HT_BRIDGE_STEP*256;
    /* Verlet inertia, gravity, hand pumping and six length-constraint passes.
     * Seven nodes / 36 segment solves is a fixed simulation budget. */
    if(m->rope_length) {
        /* The loaded support is a damped spring. A wooden limb yields more
         * than a steel crossbar; unloading lets it settle without flicker. */
        t->rope_ox[0]=t->rope_px[0];t->rope_oy[0]=t->rope_py[0];
        int limit=ht.level==0?5:2,tx=0,ty=0;
        if(t->mode==HT_ROPE) {
            tx=ht_clamp((ht.x-m->rope_anchor_x*256)/24,-limit*128,limit*128);
            ty=ht_min(limit*256,256+ht_abs(t->rope_v)/2);
        }
        t->rope_bend_vx=ht_clamp(t->rope_bend_vx+(tx-t->rope_bend_x)/12-t->rope_bend_vx/4,-96,96);
        t->rope_bend_vy=ht_clamp(t->rope_bend_vy+(ty-t->rope_bend_y)/12-t->rope_bend_vy/4,-96,96);
        t->rope_bend_x=ht_clamp(t->rope_bend_x+t->rope_bend_vx,-limit*128,limit*128);
        t->rope_bend_y=ht_clamp(t->rope_bend_y+t->rope_bend_vy,0,limit*256);
        if(!tx && ht_abs(t->rope_bend_x)<12 && ht_abs(t->rope_bend_vx)<12)t->rope_bend_x=t->rope_bend_vx=0;
        if(!ty && ht_abs(t->rope_bend_y)<12 && ht_abs(t->rope_bend_vy)<12)t->rope_bend_y=t->rope_bend_vy=0;
        int fraction,node=ht_rope_segment(&ht,&fraction);
        if(t->mode==HT_ROPE) {
            int dx=(t->rope_px[node+1]-t->rope_px[node])/256;
            int dy=(t->rope_py[node+1]-t->rope_py[node])/256;
            int length=ht_max(1,(int)ht_isqrt((unsigned)(dx*dx+dy*dy)));
            t->rope_slide_v=ht_clamp(ht_drag(t->rope_slide_v,2)+dy*18/length,-128,192);
            t->rope_grip=ht_clamp(t->rope_grip+t->rope_slide_v,0,m->rope_length*256);
            if(t->rope_grip==m->rope_length*256 || t->rope_grip==0) t->rope_slide_v=0;
            node=ht_rope_segment(&ht,&fraction);
        }
        for(int i=1;i<HT_ROPE_NODES;++i) {
            int x=t->rope_px[i],y=t->rope_py[i];
            t->rope_px[i]+=ht_clamp(ht_drag(x-t->rope_ox[i],1),-720,720)+(i==node+1 && t->mode==HT_ROPE?direction*ht_emphasis_amount(64):0);
            t->rope_py[i]+=ht_clamp(ht_drag(y-t->rope_oy[i],1),-720,720)+88;
            t->rope_ox[i]=x;t->rope_oy[i]=y;
        }
        int length=m->rope_length*256/(HT_ROPE_NODES-1);
        for(int pass=0;pass<6;++pass) {
            t->rope_px[0]=m->rope_anchor_x*256+t->rope_bend_x;t->rope_py[0]=m->rope_anchor_y*256+t->rope_bend_y;
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
static void ht_land_collision(int i,int old_x,int old_y) {
    for(int part=0;part<3;++part) {
        int l,r,top;if(!ht_platform_piece(&ht,i,part,&l,&r,&top)) break;
        int old_top=part==2?top:ht_surface_at(&ht,i,ht_clamp(old_x/256,l,r));
        if(part!=2) top=ht_surface_at(&ht,i,ht_clamp(ht.x/256,l,r));
        bool on_slope=ht.vy>=0 && ht.vy<=HT_GRAVITY && old_x>=l*256 && old_x<r*256 &&
            ht.x>=l*256 && ht.x<r*256 && ht_abs(old_y-old_top*256)<=256;
        if(on_slope) {ht.y=top*256;ht.vy=0;ht.grounded=true;}
        int bottom=ht_terrain_bottom(&ht,i,ht.x/256);
        int wall_l=l,wall_r=r;
        if(part==0 && ht.y/256-29>top+8)
            ht_cliff_bounds(&ht,i,ht.y/256-29,ht.y/256,&wall_l,&wall_r);
        ht_collide(wall_l,wall_r,top,bottom,old_x,on_slope?top*256:old_y+(top-old_top)*256);
        /* Falling along an undercut can meet a projecting lower stratum even
         * without horizontal motion. Keep the body outside that same outline. */
        if(part==0 && ht.y/256-29>top+8 && ht.y/256<bottom) {
            int old_l=l,old_r=r;
            ht_cliff_bounds(&ht,i,old_y/256-29,old_y/256,&old_l,&old_r);
            if(old_x+5*256<=old_l*256 && ht.x+5*256>wall_l*256) {ht.x=(wall_l-5)*256;ht.vx=0;}
            if(old_x-5*256>=old_r*256 && ht.x-5*256<wall_r*256) {ht.x=(wall_r+5)*256;ht.vx=0;}
        }
        /* The back of an undercut is solid too, not a visual-only wall. */
        if(ht.x>=l*256 && ht.x<r*256 && ht.y>top*256+256 && ht.y-29*256<bottom*256 &&
           old_y-29*256>=ht_terrain_bottom(&ht,i,old_x/256)*256) {ht.x=old_x;ht.vx=0;}
        if(i==ht_mech(&ht)->bridge && ht.x+5*256>l*256 && ht.x-5*256<r*256 && ht.grounded && ht.y==top*256) ht.traversal.support=4;
    }
}
/* Resolve the final pose, after every attachment and prop correction. Crossing
 * tests alone cannot recover when a tree/stone has placed feet below a slope.
 * Four bounded passes cover overlapping terraces; real recesses and the active
 * ladder's upper passage remain open. Never project through an overhead deck. */
static void ht_reconcile_ground(int old_x,int old_y) {
    ht_traversal *t=&ht.traversal;
    for(int pass=0;pass<4;++pass) {
        bool corrected=false;
        for(int i=0;i<HT_PLATFORMS;++i)for(int part=0;part<3;++part) {
            int l,r,top;if(!ht_platform_piece(&ht,i,part,&l,&r,&top))break;
            if(ht.x+5*256<=l*256 || ht.x-5*256>=r*256)continue;
            /* A ladder's authored opening permits passage through its upper
             * terrace only while attached. Its lower ground is always solid. */
            if(t->mode==HT_LADDER && i==ht_mech(&ht)->ladders[t->ladder].upper)continue;
            if(part!=2)top=ht_surface_at(&ht,i,ht_clamp(ht.x/256,l,r));
            int bottom=ht_terrain_bottom(&ht,i,ht.x/256);
            if(ht.y<=top*256 || ht.y-29*256>=bottom*256)continue;
            int wall_l=l,wall_r=r;
            if(part==0 && ht.y/256-29>top+8)
                ht_cliff_bounds(&ht,i,ht.y/256-29,ht.y/256,&wall_l,&wall_r);
            if(ht.x+5*256<=wall_l*256 || ht.x-5*256>=wall_r*256)continue;
            int up=ht.y-top*256,down=(bottom+29)*256-ht.y;
            int left=ht.x-(wall_l-5)*256,right=(wall_r+5)*256-ht.x;
            int old_top=part==2?top:ht_surface_at(&ht,i,ht_clamp(old_x/256,l,r));
            bool above=old_y<=old_top*256+256;
            bool below=old_y-29*256>=ht_terrain_bottom(&ht,i,old_x/256)*256;
            bool over=ht.x>=l*256 && ht.x<r*256;
            if(!below && ((above || up<=2*256 || (over && up<29*256)) || (up<=down && up<=left && up<=right))) {
                bool descending=ht.y>=old_y;
                ht.y=top*256;ht.vy=0;ht.grounded=true;t->support=0;
                if(i==ht_mech(&ht)->bridge)t->support=4;
                if(t->mode==HT_TREE && descending) {t->mode=HT_FREE;t->drop_cooldown=12;}
            } else if(below || (down<=left && down<=right && down<up)) {
                ht.y=(bottom+29)*256;ht.vy=ht_max(0,ht.vy);ht.grounded=false;t->support=0;
            } else {
                ht.x=(left<=right?wall_l-5:wall_r+5)*256;ht.vx=0;
            }
            corrected=true;
        }
        if(!corrected)break;
    }
}
/* The snag is solid on both sides until it becomes the landed crossing.
 * Bounded short sections rotate with the visible trunk, including its lean
 * under effort. Swept horizontal bounds also stop a fast airborne approach. */
static void ht_forest_log_collision(int old_x) {
    if(ht.level!=0 || ht.traversal.forest_log_phase==32)return;
    int angle=ht_forest_log_angle(&ht);
    int dx=312*ht_forest_log_sine(angle)/256,dy=-312*ht_forest_log_sine(4096-angle)/256;
    int px,py,pr,pb;ht_forest_log_node(dx,dy,0,&px,&py,&pr,&pb);
    int left=100000,right=-100000;
    for(int i=1;i<=32;++i) {
        int nx,ny,nr,nb;ht_forest_log_node(dx,dy,i,&nx,&ny,&nr,&nb);
        int l=ht_min(px+dy*pr/312,nx+dy*nr/312);
        int r=ht_max(px-dy*pb/312,nx-dy*nb/312);
        int top=ht_min(py-dx*pr/312,ny-dx*nr/312);
        int bottom=ht_max(py+dx*pb/312,ny+dx*nb/312);
        if(ht.y>top*256 && ht.y-29*256<bottom*256) {left=ht_min(left,l);right=ht_max(right,r);}
        px=nx;py=ny;pr=nr;pb=nb;
    }
    if(left>right)return;
    left=(left-5)*256;right=(right+5)*256;
    if(ht_max(old_x,ht.x)>left && ht_min(old_x,ht.x)<right) {
        ht.x=old_x<=(left+right)/2?left:right;ht.vx=0;
    }
}
static void ht_step_controls(int direction,int vertical,bool jump_pressed,bool jump_held) {
    bool jump_request=jump_pressed;
    int start_x=ht.x,start_y=ht.y,fall_speed=ht.vy;
    bool was_grounded=ht.grounded;
    if(ht.observation_ticks) --ht.observation_ticks;
    if(ht.landing_ticks) --ht.landing_ticks;
    ++ht.ticks;
    int pace=ht_pace_target(&ht);
    if(ht.grounded || ht.traversal.mode)
        ht.pace=(uint16_t)(ht_walk_speed(&ht)+ht_clamp(pace-ht_walk_speed(&ht),-2,2));
    ht_weather_step();
    if(ht.puzzle.solved && !ht_final_locked(&ht) && ht.puzzle.opening<48) ++ht.puzzle.opening;
    int wind=ht_wind(&ht);
    ht.weather_drift=(ht.weather_drift+wind)%(HT_W*256);
    ht_traversal *t=&ht.traversal;
    const ht_mechanics *m=ht_mech(&ht);
    ht_props_step(direction);
    uint8_t old_support=t->support;t->support=0;
    if(t->forest_push_release)--t->forest_push_release;
    if(t->mode!=HT_PUSH && t->forest_push)t->forest_push=(uint8_t)ht_max(0,t->forest_push-3);
    if(t->drop_cooldown) --t->drop_cooldown;
    if(t->branch_drop)--t->branch_drop;
    if(old_support==5 && vertical>0) {
        t->branch_drop=16;t->drop_cooldown=16;ht.grounded=false;ht.coyote=ht.jump_buffer=0;ht.y+=2*256;
    }
    if(!jump_request) {
        if(!ht_ladder_enter(vertical)) (void)ht_tree_enter(direction,vertical,jump_held);
    }
    if(t->mode==HT_LEDGE && vertical<0) {t->mode=HT_CLIMB;t->climb=t->climb_phase=0;}
    if(t->mode==HT_LEDGE && (vertical>0 || jump_pressed)) {
        t->mode=HT_FREE;t->drop_cooldown=20;ht.vy=0;ht.coyote=ht.jump_buffer=0;
        if(jump_pressed) {ht.vx=-t->ledge_side*ht_motion_speed(&ht);ht.vy=-1000;}
        jump_pressed=false;
    }
    if(jump_pressed && t->mode && t->mode!=HT_CLIMB) {
        int mode=t->mode;t->mode=HT_FREE;
        if(mode==HT_LADDER || mode==HT_TREE) t->drop_cooldown=12;
        if(mode==HT_LADDER || mode==HT_TREE || mode==HT_ROPE || mode==HT_BOAT) {
            int rvx,rvy;ht_rope_velocity(&ht,&rvx,&rvy);
            ht.vx=mode==HT_ROPE?ht_clamp(rvx,-1000,1000):(mode==HT_TREE && !direction?t->tree_side:direction)*ht_motion_speed(&ht);
            ht.vy=-HT_JUMP_SPEED+(mode==HT_ROPE?ht_clamp(rvy,-384,384):0);ht.grounded=false;ht.coyote=0;jump_pressed=false;
        }
    }
    if(t->mode==HT_PUSH) {
        if(!ht_forest_log_near(&ht) || direction<0) {t->mode=HT_FREE;ht.vx=direction*ht_motion_speed(&ht);}
        else {
            int old=ht.x;
            ht.x+=ht_clamp(350*256-ht.x,-ht_motion_speed(&ht),ht_motion_speed(&ht));
            ht.vx=ht.vy=0;ht.facing=1;ht.grounded=true;ht.stride+=ht_abs(ht.x-old);
            if(direction>0 && ht.x==350*256) {
                if(++t->forest_push==HT_FOREST_PUSH_STEPS) {
                    t->forest_log_angle=(uint16_t)ht_forest_log_angle(&ht);
                    t->forest_log_speed=0;t->forest_log_falling=true;
                    t->forest_push_release=8;t->mode=HT_FREE;
                }
            } else t->forest_push=(uint8_t)ht_max(0,t->forest_push-3);
        }
    } else if(t->mode==HT_TREE) {
        ht_climb_tree tree;bool valid=ht_existing_tree(&ht,t->tree,&tree);
        if(!valid) {t->mode=HT_FREE;ht.vy=88;}
        else {
            int old=ht.y;
            ht.y=ht_clamp(ht.y+vertical*ht_climb_speed(&ht),(tree.base-tree.height+20)*256,tree.base*256);
            ht.x=ht_tree_edge(&tree,ht.y/256,t->tree_side)*256;
            ht.vx=ht.vy=0;ht.grounded=false;ht.stride+=ht_abs(ht.y-old);
            if(direction) {
                /* Leaving the trunk always works. Step onto a nearby real
                 * limb, otherwise fall naturally: no magnetic recapture. */
                t->mode=HT_FREE;t->drop_cooldown=12;ht.vx=direction*ht_motion_speed(&ht)/2;
                for(int b=0;b<6;++b) {
                    int top;if(ht_tree_footing(&ht,&tree,b,ht.x/256,&top) && ht_abs(ht.y/256-top)<=5) {
                        ht.y=top*256;ht.grounded=true;t->support=5;break;
                    }
                }
            } else if(vertical>0 && ht.y>= (tree.base-3)*256) {
                t->mode=HT_FREE;t->drop_cooldown=12;ht.vy=88;
            }
        }
    } else if(t->mode==HT_LEDGE || t->mode==HT_CLIMB) {
        const ht_platform *p=&ht_land[t->ledge];
        int edge=t->ledge_side>0?p->left:p->right;
        if(t->mode==HT_CLIMB && t->climb<16) {++t->climb_phase;t->climb=t->climb_phase/2;}
        /* Lift clear of the lip before moving onto it; the old diagonal
         * interpolation intentionally passed through the solid corner. */
        ht.x=(edge-t->ledge_side*6)*256+t->ledge_side*18*256*ht_max(0,(int)t->climb-12)/4;
        int top=ht_surface_at(&ht,t->ledge,ht_clamp(ht.x/256,p->left,p->right-1));
        ht.y=(top+27)*256-27*256*ht_min(12,t->climb)/12;
        ht.vx=ht.vy=0;ht.grounded=false;
        if(t->climb==16) {t->mode=HT_FREE;ht.grounded=true;}
    } else if(t->mode==HT_LADDER) {
        int top,bottom;ht_ladder_ends(ht.level,t->ladder,&top,&bottom);
        ht.x=ht_ladder_x(t->ladder)*256;
        ht.y=ht_clamp(ht.y+vertical*ht_climb_speed(&ht),top*256,ht_max(ht.y,bottom*256));
        ht.vx=ht.vy=0;ht.grounded=false;
        if((ht.y==top*256 && vertical<0) || (ht.y==bottom*256 && vertical>0)) {
            t->mode=HT_FREE;ht.grounded=true;
        } else if(vertical>0 && ht.y>bottom*256) {
            /* A hand-only catch below the last rung can also drop free. */
            t->mode=HT_FREE;ht.vy=88;
        }
    } else if(t->mode==HT_ROPE) {
        int old_x=ht.x,old_y=ht.y;
        int px,py;ht_rope_grip_point(&ht,&px,&py,false);
        ht.x=px;ht.y=py+24*256;
        ht.vx=ht.x-old_x;ht.vy=ht.y-old_y;ht.grounded=false;
        for(int i=0;i<HT_PLATFORMS;++i)
            ht_land_collision(i,old_x,old_y);
        int fraction,node=ht_rope_segment(&ht,&fraction);
        int correction_x=ht.x-px,correction_y=ht.y-24*256-py;
        for(int j=ht_max(1,node);j<=node+1;++j) {t->rope_px[j]+=correction_x;t->rope_py[j]+=correction_y;}
    } else if(t->mode==HT_BOAT) {
        ht.x=t->boat_x;ht.y=ht_boat_top(&ht);
        ht.vx=t->boat_vx;ht.vy=0;ht.grounded=true;t->support=3;
    } else {
        if(jump_pressed) ht.jump_buffer=4;
        else if(ht.jump_buffer) --ht.jump_buffer;
        if(ht.grounded) ht.coyote=3;else if(ht.coyote) --ht.coyote;
        int target=direction*ht_motion_speed(&ht);
        /* Firm starts/stops on soil, small corrections in flight. Preserve
         * swing momentum instead of braking a rope release into a walk. */
        if(!ht.grounded && (!direction || (ht.vx*direction>0 && ht_abs(ht.vx)>ht_motion_speed(&ht))))target=ht.vx;
        int acceleration=ht.grounded?(direction?48:80):32;
        if(direction) acceleration=ht_emphasis_amount(acceleration);
        if(ht.vx<target) ht.vx=ht_min(target,ht.vx+acceleration);
        if(ht.vx>target) ht.vx=ht_max(target,ht.vx-acceleration);
        if(ht.jump_buffer && ht.coyote) {if(direction)ht.vx=direction*ht_motion_speed(&ht);ht.vy=-HT_JUMP_SPEED;ht.grounded=false;ht.coyote=0;ht.jump_buffer=0;}
        /* One grounded impulse: releasing B cannot cancel gravity mid-arc. */
        int old_x=ht.x,old_y=ht.y;
        ht.vy=ht_min(ht.vy+HT_GRAVITY,2400);
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
        for(int i=0;i<HT_PLATFORMS;++i) {
            if(ht.level==0 && i==1 && t->forest_log_phase<32)continue;
            const ht_platform *p=&ht_land[i];
            int platform_top=ht_surface_at(&ht,i,direction>0?p->left:p->right-1);
            if(!t->mode && !t->drop_cooldown && !ht.grounded && direction &&
               ht.y>=(platform_top+12)*256 && ht.y<=(platform_top+32)*256 &&
               ht_abs(ht.x/256-(direction>0?p->left:p->right))<=9 &&
               (direction>0?old_x<p->left*256:old_x>p->right*256)) {
                t->mode=HT_LEDGE;t->ledge=(uint8_t)i;t->ledge_side=direction;t->climb=0;
                ht.x=((direction>0?p->left:p->right)-direction*6)*256;
                ht.y=(platform_top+27)*256;ht.vx=ht.vy=0;ht.coyote=ht.jump_buffer=0;
                break;
            }
            ht_land_collision(i,old_x,old_y);
        }
        if(!t->mode)ht_tree_collision(old_x,old_y,old_support);
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
            if(old_support==3 && ht.vy>=0 && ht_abs(ht.x-t->boat_x)<(ht_boat_half(&ht)-1)*256 && ht_abs(old_y-ht_boat_top(&ht))<3*256) {
                ht.y=top*256;ht.vy=0;ht.grounded=true;
            } else ht_collide(t->boat_x/256-ht_boat_half(&ht)+1,t->boat_x/256+ht_boat_half(&ht)-1,top,top+10,old_x,old_y);
            if(ht.grounded && ht.y==top*256 && ht_abs(ht.x-t->boat_x)<(ht_boat_half(&ht)+4)*256) t->support=3;
        }
    }
    if(ht.level==4 && ht.x/256<520) {
        int ceiling=ht_grotto_ceiling(ht.x/256);
        if(ht.y-29*256<ceiling*256) {ht.y=(ceiling+29)*256;ht.vy=ht_max(0,ht.vy);}
    }
    if(!jump_request) (void)ht_ladder_enter(vertical);
    ht_forest_log_collision(start_x);
    ht_reconcile_ground(start_x,start_y);
    t->push_hint=0;
    if(t->mode==HT_FREE && ht.grounded && direction && !ht.vx) {
        int near=ht_traversal_near(&ht);
        int object=near==HT_ROLL?t->ball_x:near==HT_CRATE?t->crate_x:ht.x;
        if((near==HT_ROLL || near==HT_CRATE) && (object-ht.x)*direction>0)
            t->push_hint=(uint8_t)near;
        else if(direction>0 && ht_forest_log_near(&ht))t->push_hint=HT_PUSH;
    }
    ht_camera_step((direction || vertical || t->mode==HT_CLIMB) && (start_x!=ht.x || start_y!=ht.y));
    if(ht.grounded && !was_grounded && fall_speed>384 && !t->mode) {
        ht.landing_ticks=10;ht.landing_strength=(uint8_t)ht_clamp(fall_speed/256,2,8);
        ht.landing_x=ht.x/256;ht.landing_y=ht.y/256;
    }
    if(direction) ht.facing=direction;
    if(ht.grounded) {
        ht.stride+=ht_abs(ht.x-start_x);
        for(int i=3;i<=9;i+=3) if(ht.x/256>=ht_land[i].left && ht.x/256<ht_land[i].right && ht.y==ht_surface_at(&ht,i,ht.x/256)*256) ht.checkpoint=i;
    }
    if((!ht.puzzle.solved || ht.puzzle.opening<48 || ht_final_locked(&ht)) && ht.x>HT_PUZZLE_GATE*256) {ht.x=HT_PUZZLE_GATE*256;ht.vx=0;}
    ht.story_x=ht_max(ht.story_x,ht.x/256);
    unsigned beat=ht_story_beat_at(&ht)+1;
    if(ht.story_seen[ht.level]<beat) ht.story_seen[ht.level]=(uint8_t)beat;
    if(ht.y>500*256) {
        ++ht.deaths;ht_spawn(false);return;
    }
    if(ht.x>=HT_GOAL*256 && ht.grounded && ht.puzzle.solved && ht.puzzle.opening==48 && !ht_final_locked(&ht)) {
        if(++ht.level==HT_LEVELS) {ht.level=0;++ht.laps;} ht_spawn(true);return;
    }
    ht.camera+=(ht_clamp(ht.x/256-200,-120,HT_GOAL-260)*256-ht.camera)/8;
    ht.camera_y+=(ht_clamp(ht.y/256-180-ht_vista_lift(&ht),-300,100)*256-ht.camera_y)/8;
}
/* Keep the simple horizontal input entry point for SDK examples and tests. */
static void ht_step(int direction,bool jump_pressed,bool jump_held) { ht_step_controls(direction,0,jump_pressed,jump_held); }
