/* One articulated silhouette for every activity. Contacts are authored in
 * world pixels; two-bone joints use Q8 lengths before the final raster rounding. */
typedef struct {int x,y;} ht_joint;
typedef struct {
    ht_joint hip,shoulder,hand[2],foot[2];
    int facing,knee[2],elbow[2];
} ht_person_pose;
static ht_joint ht_joint_at(int x,int y) {ht_joint p={x,y};return p;}
static int ht_character_run_blend(const ht_game *g) {
    return g->grounded && g->traversal.mode==HT_FREE?ht_clamp((ht_abs(g->vx)-304)*8,0,256):0;
}
/* The endpoint remains on its contact. Reach is clamped only for calculating
 * the bend: normal targets fit the limbs; a stretched transition stays joined. */
static ht_joint ht_person_joint(ht_joint a,ht_joint b,int upper,int lower,int bend) {
    int dx=(b.x-a.x)*256,dy=(b.y-a.y)*256;
    int actual=(int)ht_isqrt((unsigned)(dx*dx+dy*dy));
    if(!actual)return ht_joint_at(a.x+bend*upper,a.y);
    int d=ht_clamp(actual,ht_abs(upper-lower)*256+1,(upper+lower)*256-1);
    int along=(d*d+(upper*upper-lower*lower)*65536)/(2*d);
    int height=(int)ht_isqrt((unsigned)ht_max(0,upper*upper*65536-along*along));
    return ht_joint_at(a.x+(dx*along-dy*height*bend)/actual/256,
                       a.y+(dy*along+dx*height*bend)/actual/256);
}
static int ht_person_rung(int world,int top,int offset,int side) {
    int relative=world-top-6-offset,rung=ht_floor_div(relative,20);
    int phase=relative-rung*20;
    return top+6+rung*20+(side<0?ht_max(0,phase-10)*2:-10+ht_min(phase,10)*2);
}
static int ht_person_ground(const ht_game *g,int x,int feet) {
    if(!g->grounded || g->traversal.support)return feet;
    int world=x+g->camera/256;
    if(g->door_stage)return ht_door_ground(world)-g->camera_y/256;
    for(int i=0;i<HT_PLATFORMS;++i)for(int part=0;part<3;++part) {
        int l,r,top;if(!ht_platform_piece(g,i,part,&l,&r,&top))break;
        if(world<l || world>=r)continue;
        if(part!=2)top=ht_surface_at(g,i,world);
        top-=g->camera_y/256;
        if(ht_abs(top-feet)<=5)return top;
    }
    return feet;
}
static ht_person_pose ht_person_pose_at(int x,int feet,const ht_game *g) {
    ht_person_pose p;int f=g->facing?g->facing:1;
    p.facing=f;p.hip=ht_joint_at(x,feet-14);p.shoulder=ht_joint_at(x+f,feet-22);
    for(int i=0;i<2;++i) {
        p.foot[i]=ht_joint_at(x+(i?2:-2),feet);
        p.hand[i]=ht_joint_at(x+f*(i?2:-1),feet-12);
        p.knee[i]=-f;p.elbow[i]=f;
    }
    const ht_traversal *t=&g->traversal;
    if(t->mode==HT_FREE && t->push_hint && g->grounded) {
        /* Render the real contact pose, then halve every displacement from
         * relaxed stance. A copy keeps this visual hint out of game physics. */
        ht_game contact=*g;contact.traversal.mode=t->push_hint;
        contact.traversal.push_hint=0;
        contact.traversal.forest_push=HT_FOREST_PUSH_STEPS/2;
        ht_person_pose effort=ht_person_pose_at(x,feet,&contact);
        p.hip.x+=(effort.hip.x-p.hip.x)/2;p.hip.y+=(effort.hip.y-p.hip.y)/2;
        p.shoulder.x+=(effort.shoulder.x-p.shoulder.x)/2;p.shoulder.y+=(effort.shoulder.y-p.shoulder.y)/2;
        for(int i=0;i<2;++i) {
            p.hand[i].x+=(effort.hand[i].x-p.hand[i].x)/2;p.hand[i].y+=(effort.hand[i].y-p.hand[i].y)/2;
            p.foot[i].x+=(effort.foot[i].x-p.foot[i].x)/2;p.foot[i].y+=(effort.foot[i].y-p.foot[i].y)/2;
            p.knee[i]=effort.knee[i];p.elbow[i]=effort.elbow[i];
        }
        p.facing=effort.facing;return p;
    }
    if(t->mode==HT_FREE && !t->forest_push_release) {
        int run=ht_character_run_blend(g),phase=(g->stride/256)%32;
        bool moving=g->grounded && ht_abs(g->vx)>20;
        int compression=g->landing_ticks?ht_min(3,(int)g->landing_ticks*g->landing_strength/20):0;
        int flight=moving?ht_max(0,2-ht_abs(phase%16-8))*run/256:0;
        p.hip.y+=compression-flight;p.shoulder.y+=compression-flight;
        p.shoulder.x+=f*(2*run/256);
        for(int i=0;i<2;++i) {
            int q=(phase+i*16)%32;
            /* Stance foot travels backward exactly with world displacement.
             * Recovery arcs forward with a bent knee and clears the soil. */
            int step=q<16?8-q:q-24;
            int lift=q<16?0:(q-16)*(32-q)*(3+run*6/256)/64;
            if(moving) {
                p.foot[i].x=x+f*step;p.foot[i].y=ht_person_ground(g,p.foot[i].x,feet)-lift-flight;
                p.hand[i].x=x-f*step/2+f*run/128;
                p.hand[i].y=feet-12-run*6/256-flight+compression;
            } else p.foot[i].y=ht_person_ground(g,p.foot[i].x,feet);
        }
        if(!g->grounded) {
            int rise=ht_clamp(-g->vy/160,0,8),fall=ht_clamp(g->vy/200,0,6);
            p.shoulder.x=x+f*2;p.hip.y=feet-13;
            p.foot[0]=ht_joint_at(x+f*(4+rise/2),feet-2-rise/2);
            p.foot[1]=ht_joint_at(x-f*5,feet-1-rise);
            p.hand[0]=ht_joint_at(x+f*7,feet-21-rise/2+fall);
            p.hand[1]=ht_joint_at(x-f*6,feet-18-rise/2+fall);
        }
    } else if(t->mode==HT_WINDOW) {
        int phase=t->window_phase,q=phase-32,cx=g->camera/256,cy=g->camera_y/256;
        int contact=ht_max(t->window_touch?t->window_touch+1:0,ht_min(ht_clamp(phase*256/16,0,256),ht_clamp((144-phase)*256/8,0,256)));
        for(int i=0;i<2;++i) {
            /* A hand grips a real jamb only while reachable. In the middle
             * both hands relax while alternating feet stand on the sill;
             * never slide an imaginary grip through the open aperture. */
            int shoulder=p.shoulder.x+cx,left=HT_SIGNAL_WINDOW_LEFT+5,right=HT_SIGNAL_WINDOW_RIGHT-5;
            int jamb=ht_abs(shoulder-left)<ht_abs(shoulder-right)?left:right;
            int reach=ht_clamp((13-ht_abs(shoulder-jamb))*64,0,256)*contact/256;
            int hx=jamb-cx,hy=g->y/256-22+i*3-cy;
            p.hand[i].x+=(hx-p.hand[i].x)*reach/256;
            p.hand[i].y+=(hy-p.hand[i].y)*reach/256;p.elbow[i]=1;
            int start=t->window_entry_left/256+(i?2:-2),end=t->window_entry_right/256+(i?2:-2),wx,wy;
            if(q<40) {
                wx=ht_shutter_lerp(start,HT_SIGNAL_WINDOW_LEFT+12+i*2,q,i?22:28,i?36:40);
                wy=ht_shutter_lerp(ht_signal_window_floor(),ht_signal_window_sill(),q,i?10:16,i?22:28);
            } else if(q<=72) {
                static const int8_t offset[8]={2,1,1,0,0,-1,0,1};
                int gait=(q-40+i*4)&7;
                wx=g->x/256+(i?2:-2)+offset[gait];
                wy=ht_signal_window_sill()-(gait>=5?(gait==6?2:1):0);
            } else {
                wx=ht_shutter_lerp(HT_SIGNAL_WINDOW_RIGHT-12+i*2,end,q,i?80:88,i?96:104);
                wy=ht_shutter_lerp(ht_signal_window_sill(),ht_signal_window_floor(),q,i?92:104,i?104:112);
            }
            p.foot[i]=ht_joint_at(wx-cx,wy-cy);p.knee[i]=-1;
        }
    } else if(t->mode==HT_SHUTTER) {
        int q=t->mill_entry_phase,cx=g->camera/256,cy=g->camera_y/256;
        int contact=ht_min(ht_clamp(q*256/16,0,256),ht_clamp((96-q)*256/8,0,256));
        /* Knees lift onto the drawn sill. The hands change jambs only while
         * crossing, then release onto the original interior floor. */
        p.hip.y+=3*contact/256;p.shoulder.y+=3*contact/256;
        for(int i=0;i<2;++i) {
            int hx=ht_shutter_lerp(HT_MILL_SHUTTER_LEFT+1,HT_MILL_SHUTTER_RIGHT,q,32+i*8,52+i*8)-cx;
            int hy=HT_MILL_SHUTTER_SILL-11+i*2-cy;
            p.hand[i].x+=(hx-p.hand[i].x)*contact/256;
            p.hand[i].y+=(hy-p.hand[i].y)*contact/256;
            int begin=t->mill_entry_left/256+(i?2:-2),end=t->mill_entry_right/256+(i?2:-2);
            int sill=HT_MILL_SHUTTER_LEFT+3+i*5;
            int wx=q<48?ht_shutter_lerp(begin,sill,q,i?26:32,i?38:40):
                ht_shutter_lerp(sill,end,q,i?72:64,i?82:74);
            int start=ht_land_height(0,3,begin),finish=ht_land_height(0,3,end);
            int wy=q<48?ht_shutter_lerp(start,HT_MILL_SHUTTER_SILL,q,i?16:24,i?26:32):
                ht_shutter_lerp(HT_MILL_SHUTTER_SILL,finish,q,i?82:74,i?88:82);
            p.foot[i]=ht_joint_at(wx-cx,wy-cy);p.knee[i]=-1;p.elbow[i]=1;
        }
    } else if(t->mode==HT_PUSH || t->forest_push_release) {
        int load=t->forest_push,release=t->forest_push_release;
        int pulse=load?(int)(g->ticks%20):0;if(pulse>10)pulse=20-pulse;
        int lean=2+load/16,crouch=1+pulse/5,reach=14;
        if(release){lean=release/2;crouch=release/4;reach=6+release;}
        p.facing=1;p.hip=ht_joint_at(x-3,feet-11);
        p.shoulder=ht_joint_at(x+lean,feet-21+crouch);
        p.foot[0]=ht_joint_at(x-11,feet);p.foot[1]=ht_joint_at(x+5,feet);
        p.hand[0]=ht_joint_at(x+reach,feet-19);p.hand[1]=ht_joint_at(x+reach,feet-15);
        p.knee[0]=p.knee[1]=-1;
    } else if(t->mode==HT_ROLL || t->mode==HT_CRATE) {
        int object=t->mode==HT_ROLL?t->ball_x:t->crate_x,side=object>=g->x?1:-1;
        int v=t->mode==HT_ROLL?t->ball_vx:t->crate_vx;
        bool pulling=(ht_abs(v)>24?v:g->facing)*side<0;
        int lean=pulling?-side:side,step=ht_abs(v)>24?(g->stride/256)%16:8;
        step=step<8?step-4:12-step;
        int hand=object/256-g->camera/256-side*(t->mode==HT_ROLL?HT_BALL_RADIUS-2:HT_CRATE_HALF);
        int hy=t->mode==HT_ROLL?t->ball_y/256-g->camera_y/256-4:t->crate_y/256-g->camera_y/256-ht_crate_height(g->level)+(g->level==0?1:8);
        p.facing=side;p.hip=ht_joint_at(x-lean*2,feet-12);
        p.shoulder=ht_joint_at(x+lean*(pulling?5:4),feet-20);
        for(int i=0;i<2;++i) {
            p.foot[i]=ht_joint_at(x-lean*(i?8:0)+(i?step:-step),feet);
            p.hand[i]=ht_joint_at(hand,hy+i*3);p.knee[i]=-side;p.elbow[i]=side;
        }
    } else if(t->mode==HT_TREE || t->mode==HT_LADDER) {
        int side=t->mode==HT_TREE?-t->tree_side:f;
        int world=g->y/256,phase=ht_floor_div(world,1)%24;if(phase<0)phase+=24;
        p.facing=side;p.hip.x=x-side*2;p.shoulder.x=x+side;
        for(int i=0;i<2;++i) {
            int q=(phase+i*12)%24;
            /* Each grip holds a fixed world height for half the cycle. */
            int lift=q<12?q:24-q;
            int hx=x+side*6,fx=x+side*5;
            if(t->mode==HT_LADDER){hx=x+(i?5:-5);fx=x+(i?4:-4);}
            p.hand[i]=ht_joint_at(hx,feet-22-lift);
            p.foot[i]=ht_joint_at(fx,feet-1-(12-lift)/2);
            if(t->mode==HT_TREE) {
                ht_climb_tree tree;
                if(ht_existing_tree(g,t->tree,&tree)) {
                    int hand_world=p.hand[i].y+g->camera_y/256;
                    int foot_world=p.foot[i].y+g->camera_y/256;
                    p.hand[i].x=ht_tree_edge(&tree,hand_world+20,t->tree_side)+side*5-g->camera/256;
                    p.foot[i].x=ht_tree_edge(&tree,foot_world+20,t->tree_side)+side*5-g->camera/256;
                }
            } else {
                int top,bottom;ht_ladder_ends(g->level,t->ladder,&top,&bottom);
                p.hand[i].y=ht_clamp(ht_person_rung(world,top,23,i?1:-1),top,bottom)-g->camera_y/256;
                p.foot[i].y=ht_clamp(ht_person_rung(world,top,1,i?-1:1),top,bottom)-g->camera_y/256;
            }
            p.knee[i]=-side;p.elbow[i]=side;
        }
    } else if(t->mode==HT_LEDGE || t->mode==HT_CLIMB) {
        const ht_platform *land=&ht_level_land(g->level)[t->ledge];
        int side=t->ledge_side,world=side>0?land->left:land->right-1;
        int edge=world-g->camera/256,lip=ht_surface_at(g,t->ledge,world)-g->camera_y/256;
        int phase=t->climb;p.facing=side;p.shoulder.x=x+side*2;
        for(int i=0;i<2;++i) {
            p.hand[i]=ht_joint_at(edge+side*(2+i*3+ht_max(0,phase-9)),lip);
            p.elbow[i]=side;p.knee[i]=-side;
        }
        p.foot[0]=ht_joint_at(x-side*4,feet-1);
        p.foot[1]=phase<7?ht_joint_at(x+side*4,feet-5):ht_joint_at(edge+side*(5+phase-7),lip);
    } else if(t->mode==HT_ROPE) {
        int gx,gy;ht_rope_grip_point(g,&gx,&gy,false);
        gx=gx/256-g->camera/256;gy=gy/256-g->camera_y/256;
        int trail=ht_clamp(g->vx/160,-5,5);
        p.hip.x=x-trail;p.shoulder=ht_joint_at(x-trail/2,feet-18);
        p.hand[0]=ht_joint_at(gx,gy);p.hand[1]=ht_joint_at(gx,gy+2);
        p.foot[0]=ht_joint_at(x-trail-3,feet-2);p.foot[1]=ht_joint_at(x-trail+4,feet-5);
    } else if(t->mode==HT_BOAT) {
        int stroke=ht_boat_stroke(g);
        p.hip=ht_joint_at(x-2,feet-1);p.shoulder=ht_joint_at(x+stroke/4,feet-11);
        for(int i=0;i<2;++i) {
            p.hand[i]=ht_joint_at(x+5+stroke/2,feet-6+i);
            p.foot[i]=ht_joint_at(x+f*(9+i*2),feet+4);
            p.knee[i]=-f;
        }
    }
    return p;
}
static void ht_person_arm(ht_joint root,ht_joint hand,int bend,int ink) {
    ht_joint elbow=ht_person_joint(root,hand,6,6,bend);
    ht_limb(ht_scene,root.x,root.y,elbow.x,elbow.y,2,1,ink);
    ht_limb(ht_scene,elbow.x,elbow.y,hand.x,hand.y,1,1,ink);
    ht_disk(ht_scene,hand.x,hand.y,1,ink);
}
static void ht_person_leg(ht_joint hip,ht_joint foot,int bend,int f,int ink) {
    ht_joint knee=ht_person_joint(hip,ht_joint_at(foot.x,foot.y-1),8,7,bend);
    ht_limb(ht_scene,hip.x,hip.y,knee.x,knee.y,2,1,ink);
    ht_limb(ht_scene,knee.x,knee.y,foot.x,foot.y-1,1,1,ink);
    /* Heel, ankle and a low toe rather than a rectangular paddle. */
    ht_triangle(ht_scene,foot.x-f,foot.y,foot.x,foot.y-2,foot.x+f*3,foot.y,ink);
}
static void ht_person_draw(const ht_person_pose *p) {
    int sx=p->shoulder.x,sy=p->shoulder.y,hx=p->hip.x,hy=p->hip.y,f=p->facing;
    ht_person_leg(p->hip,p->foot[0],p->knee[0],f,235);
    ht_person_arm(p->shoulder,p->hand[0],p->elbow[0],235);
    /* Ordinary coat over a upright torso, with a soft shoulder/hem rather
     * than pointed lapels. The novella supplies a coat and a carried bag. */
    ht_limb(ht_scene,sx,sy+1,hx,hy-1,3,3,255);
    ht_limb(ht_scene,hx-2,hy,hx+2,hy,1,1,255);
    ht_limb(ht_scene,sx,sy-1,sx,sy-3,1,1,255);
    /* Rounded crown/nape and a restrained profile. No hat-like point,
     * jutting beak or glowing face details at this small physical scale. */
    ht_disk(ht_scene,sx,sy-5,3,255);
    ht_limb(ht_scene,sx+f*2,sy-5,sx+f*3,sy-4,0,0,255);
    ht_limb(ht_scene,sx,sy-3,sx+f,sy-2,1,1,255);
    ht_person_leg(p->hip,p->foot[1],p->knee[1],f,255);
    ht_person_arm(p->shoulder,p->hand[1],p->elbow[1],255);
    /* A few subdued clothing seams survive close framing without stippling. */
    if(ht_world_scale>=320) {
        ht_limb(ht_scene,sx-f,sy+2,hx-f,hy-2,0,0,211);
        ht_limb(ht_scene,sx-f,sy,sx+f,sy+1,0,0,200);
    }
}
static void ht_character_walk(int x,int feet,const ht_game *g) {
    ht_person_pose p=ht_person_pose_at(x,feet,g);ht_person_draw(&p);
    if(g->traversal.mode==HT_WINDOW && g->traversal.window_touch) {
        for(int i=0;i<4;++i) {
            int length=3+i%3,release=255-g->traversal.window_touch-i*42;
            length-=ht_clamp(release/12,0,length);
            if(length)ht_limb(ht_scene,p.hand[0].x+i,p.hand[0].y,p.hand[0].x+i,p.hand[0].y-length,0,0,235);
        }
    }
}
static bool ht_character_instrument(int x,int feet,const ht_game *g) {
    if(g->traversal.mode==HT_FREE)return false;
    ht_character_walk(x,feet,g);return true;
}
