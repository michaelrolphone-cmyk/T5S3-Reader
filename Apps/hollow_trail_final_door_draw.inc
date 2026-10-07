/* The final street uses the ordinary primitive raster and articulated body.
 * Fixed world surfaces are shared by draw and feet. Distant silhouettes carry
 * a narrow, diffuse gray range; the near wall/door/coat retain solid contact. */
static void ht_text_color(int x,int y,const char *text,int scale,int color);
static void ht_narration_hash(const char *text);
static void ht_door_lamp(int x,int y) {
    ht_rect(ht_scene,x-3,y-8,7,8,235);
    ht_rect(ht_scene,x-2,y-7,5,5,35);
    ht_limb(ht_scene,x-2,y-9,x+2,y-9,1,1,240);
    ht_limb(ht_scene,x-3,y-8,x-2,y-12,0,0,230);
    ht_limb(ht_scene,x-2,y-12,x+2,y-12,0,0,230);
    ht_limb(ht_scene,x+2,y-12,x+3,y-8,0,0,230);
    ht_rect(ht_scene,x,y-6,1,3,0);
}
static void ht_door_shoe(int x,int y,bool upright) {
    if(upright) {
        ht_limb(ht_scene,x-3,y-1,x+3,y-1,1,1,252);
        ht_disk(ht_scene,x-2,y-3,2,245);ht_disk(ht_scene,x-2,y-3,0,100);
        ht_limb(ht_scene,x-3,y,x+4,y,0,0,240);
        for(int i=0;i<3;++i)ht_limb(ht_scene,x+i,y-3,x+i+1,y-1,0,0,153);
        ht_limb(ht_scene,x,y-3,x+3,y-5,0,0,210);
        ht_limb(ht_scene,x+3,y-5,x+5,y-3,0,0,210);
    } else {
        ht_limb(ht_scene,x-2,y-1,x+2,y-3,1,1,248);
        ht_limb(ht_scene,x-3,y,x+3,y-3,0,0,135);
        ht_limb(ht_scene,x+2,y-3,x+4,y-1,0,0,216);
    }
}
static void ht_door_draw_yard(const ht_game *g,int camera,int cy) {
    int floor=ht_door_ground(HT_DOOR_BARREL)-cy,x=HT_DOOR_BARREL-camera;
    /* Blank tower-facing wall, masonry and actual descending stair. */
    ht_rect(ht_scene,-camera,28-cy,452,168,183);
    for(int row=0;row<7;++row) {
        int y=39+row*23-cy;
        ht_limb(ht_scene,8-camera,y,445-camera,y+1,0,0,162);
        for(int col=0;col<5;++col) {
            int sx=15+col*102+(row%2)*45-camera;
            ht_limb(ht_scene,sx,y,sx-2,y+21,0,0,169);
        }
    }
    ht_rect(ht_scene,5-camera,20-cy,71,137,248);
    ht_limb(ht_scene,14-camera,154-cy,145-camera,187-cy,2,2,230);
    for(int step=0;step<12;++step) {
        int sx=step*12-camera,y=ht_door_ground(step*12)-cy;
        ht_rect(ht_scene,sx,y,12,ht_max(1,198-cy-y),233);
        ht_limb(ht_scene,sx,y,sx+12,y,1,1,132);
    }
    /* Low passage: roof mass occludes, nothing blinking points through it. */
    ht_rect(ht_scene,414-camera,-cy,70,134,242);
    ht_rect(ht_scene,418-camera,133-cy,11,63,239);
    ht_limb(ht_scene,429-camera,134-cy,481-camera,146-cy,3,3,221);
    /* Broken spout, water surface and staved barrel. */
    ht_limb(ht_scene,x-18,87-cy,x-18,143-cy,3,3,239);
    ht_limb(ht_scene,x-18,143-cy,x+12,145-cy,3,2,239);
    ht_limb(ht_scene,x+12,145-cy,x+13,141-cy,1,0,215);
    ht_rect(ht_scene,x-16,floor-32,37,31,237);
    ht_disk(ht_scene,x+2,floor-31,18,220);
    ht_rect(ht_scene,x-15,floor-30,34,11,237);
    ht_limb(ht_scene,x-12,floor-32,x+14,floor-32,2,2,88);
    for(int i=0;i<5;++i)ht_limb(ht_scene,x-12+i*7,floor-26,x-11+i*7,floor-3,0,0,182);
    ht_limb(ht_scene,x-15,floor-22,x+20,floor-22,1,1,155);
    ht_limb(ht_scene,x-15,floor-7,x+20,floor-7,1,1,155);
    unsigned drip=g->door_stage==HT_DOOR_REST?g->door_tick:0;
    unsigned fall=drip%127;
    if(fall<12 || (fall>=29 && fall<41))ht_disk(ht_scene,x+13,151-cy+(int)(fall%29),0,72);
    /* Chair rests upside down, its stable geometry separate from its shadow. */
    int chair=x+62;
    int bend=g->door_stage==HT_DOOR_REST && g->door_tick>=160 && g->door_tick<340?
        ht_max(0,18-ht_abs((int)g->door_tick-244)/5):0;
    ht_limb(ht_scene,chair-6,172-cy,chair+38+bend,108-cy,3,2,204);
    ht_limb(ht_scene,chair+12,172-cy,chair+63+bend,114-cy,3,2,204);
    ht_limb(ht_scene,chair+36+bend,111-cy,chair+62+bend,117-cy,3,3,204);
    ht_limb(ht_scene,chair-9,184-cy,chair+14,184-cy,2,2,253);
    ht_limb(ht_scene,chair-7,184-cy,chair-12,157-cy,1,1,253);
    ht_limb(ht_scene,chair+11,184-cy,chair+15,157-cy,1,1,253);
    ht_limb(ht_scene,chair-9,184-cy,chair-5,196-cy,1,1,253);
    ht_limb(ht_scene,chair+14,184-cy,chair+19,196-cy,1,1,253);
    ht_limb(ht_scene,chair-5,194-cy,chair+19,194-cy,1,1,253);
    /* Rooted grass only in joints. */
    for(int i=0;i<8;++i) {
        int gx=164+i*29-camera;
        ht_limb(ht_scene,gx,198-cy,gx-2,192-cy,0,0,247);
        ht_limb(ht_scene,gx,198-cy,gx+3,194-cy,0,0,247);
    }
}
static bool ht_door_seated(const ht_game *g) {
    return g->door_stage==HT_DOOR_REST || (g->door_stage>=HT_DOOR_SHOES && g->door_stage<=HT_DOOR_STRAIGHTEN);
}
static ht_person_pose ht_door_pose(const ht_game *g) {
    int x=g->x/256-g->camera/256,feet=g->y/256-g->camera_y/256;
    int shoe=HT_DOOR_SHOE_X-g->camera/256,base=ht_door_ground(1152)-g->camera_y/256;
    ht_person_pose p=ht_person_pose_at(x,feet,g);
    bool seated=ht_door_seated(g);
    if(seated) {
        p.hip=ht_joint_at(x,feet-6);p.shoulder=ht_joint_at(x+2,feet-15);
        p.foot[0]=ht_joint_at(x+6,feet);p.foot[1]=ht_joint_at(x+11,feet);
        p.hand[0]=ht_joint_at(x+8,feet-7);p.hand[1]=ht_joint_at(x+10,feet-7);
        if(g->door_stage==HT_DOOR_SHOES || g->door_stage==HT_DOOR_STRAIGHTEN) {
            /* Bring the body over the planted feet before reaching. Both
             * targets fit the same 6+6 arm and 8+7 leg lengths as walking. */
            int lean=g->door_stage==HT_DOOR_STRAIGHTEN?7:0;
            p.hip=ht_joint_at(x+3+lean,feet-7);p.shoulder=ht_joint_at(x+10+lean,feet-10);
            p.hand[0]=ht_joint_at(x+8+lean,feet-3);
            p.hand[1]=ht_joint_at(shoe+(g->door_stage==HT_DOOR_STRAIGHTEN?10:0),base-3);
        }
        if(g->door_stage==HT_DOOR_GRIEF) {
            int rock=g->door_tick<320?(int)(g->door_tick%86)/43:0;
            p.shoulder.x+=rock;p.shoulder.y+=3;
            p.hand[0]=ht_joint_at(x+5,feet-17);
            p.hand[1]=ht_joint_at(x+8,feet-11);
        }
    }
    if(g->door_stage>=HT_DOOR_KNOCK) {
        /* Single ordinary, irregular hand action. It is never a signal. */
        p.shoulder.x=x+4;p.hip.x=x+2;
        p.hand[1]=ht_joint_at(x+15,feet-19);
        if(g->door_stage==HT_DOOR_KNOCK &&
           ((g->door_tick>=46 && g->door_tick<56)||(g->door_tick>=79 && g->door_tick<86)))p.hand[1].x-=3;
        p.hand[0]=ht_joint_at(x-1,feet-13);
    }
    return p;
}
static void ht_door_render(const ht_game *source,bool character) {
    memset(&ht_render_last,0,sizeof(ht_render_last));
    ht_game g=*source;int camera=g.camera/256,cy=g.camera_y/256;
    uint32_t start=ht_render_now();
    ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;
    ht_native_foreground_half_y=false;ht_scene=ht_native_active?ht_native_a:ht_scene_low;
    ht_framed=false; /* Baseline packing must not inherit the tower's rounded edge. */
    ht_world_scale=256;ht_select_level(g.level);
    memset(ht_scene,151,ht_native_active?HT_NATIVE_PIXELS:HT_PIXELS);
    /* Receding valley and roofs: no figures, no wire, no emissive windows. */
    for(int layer=0;layer<3;++layer) {
        int parallax=camera/(5-layer),ink=156+layer*10;
        for(int i=-2;i<12;++i) {
            int x=i*81-parallax%81,y=113+layer*18+(int)(ht_hash((unsigned)(i+31*layer))%15);
            ht_rect(ht_scene,x,y,79,130,ink);
            ht_triangle(ht_scene,x-6,y,x+31,y-19,x+85,y,ink);
            if((i&3)==0)ht_checkpoint();
        }
    }
    ht_render_last.background=ht_render_now()-start;start=ht_render_now();
    ht_world_scale=HT_DOOR_SCALE;
    /* Near houses overlap the diffuse valley. Leave a single parapet gap. */
    for(int house=0;house<5;++house) {
        int wx=490+house*163,x=wx-camera,base=ht_door_ground(wx)-cy;
        if(house==2)continue;
        int roof=base-117-(house%2)*18;
        /* The final two houses meet: no bright sky slit points at the door. */
        ht_rect(ht_scene,x,roof,house==3?163:150,base-roof,219);
        ht_triangle(ht_scene,x-8,roof,x+59,roof-21,x+157,roof,238);
        ht_rect(ht_scene,x+22,roof+27,25,31,231);
        ht_rect(ht_scene,x+24,roof+29,21,27,209);
        ht_rect(ht_scene,x+92,base-71,40,72,235);
        ht_limb(ht_scene,x,base-6,x+149,base-5,1,1,190);
        for(int row=0;row<4;++row) {
            int y=base-13-row*19;
            ht_limb(ht_scene,x+7,y,x+142,y+1,0,0,208);
        }
        ht_checkpoint();
    }
    /* Tree's twigs disappear against the valley, distinct only at the sky. */
    int tree=847-camera,ty=ht_door_ground(847)-cy;
    ht_rect(ht_scene,802-camera,ty-28,115,28,228);
    ht_limb(ht_scene,tree,ty-18,tree-8,ty-94,3,1,227);
    for(int i=0;i<5;++i) {
        int y=ty-40-i*11;
        ht_limb(ht_scene,tree-4,y,tree-33+i*5,y-27,1,0,220);
        ht_limb(ht_scene,tree-4,y-7,tree+28-i*3,y-31,1,0,220);
    }
    ht_limb(ht_scene,tree+14,ty-104,tree+21,ty-102,0,0,233);
    /* Settled paving and gutter are continuous with the contact function. */
    for(int sx=-8;sx<488;++sx) {
        int wx=sx+camera,ground=ht_door_ground(wx)-cy;
        ht_rect(ht_scene,sx,ground,1,80,227);
        if(wx%29==0)ht_limb(ht_scene,sx,ground+1,sx+7,ground+17,0,0,170);
        int gutter=ground+15+(wx/85)%3;
        ht_rect(ht_scene,sx,gutter,1,2,247);
        if(wx%13<7)ht_rect(ht_scene,sx,ground+5,1,1,184);
        if((sx&31)==0)ht_checkpoint();
    }
    if(camera<490)ht_door_draw_yard(&g,camera,cy);
    ht_checkpoint();
    int door=1119-camera,base=ht_door_ground(1152)-cy;
    /* A human-scale closed door, lit from the low pantry lamp. A soft elliptical
     * pool reaches the worn latch and falls away, without a rectangular beacon. */
    ht_rect(ht_scene,door-3,base-50,45,51,246);
    ht_rect(ht_scene,door,base-48,39,46,239);
    ht_person_pose pose=ht_door_pose(&g);
    int lampx=pose.hand[0].x,lampy=pose.hand[0].y+12;
    if(ht_door_seated(&g)) {
        lampx=g.x/256-camera+7;lampy=g.y/256-cy;
    }
    for(int y=base-47;y<base;++y) {
        for(int x=door+1;x<door+38;++x) {
            int dx=x-lampx,dy=y-lampy;
            int light=ht_max(0,102-(dx*dx/40+dy*dy/14));
            ht_rect(ht_scene,x,y,1,1,239-light);
        }
        if((y&7)==0)ht_checkpoint();
    }
    for(int i=0;i<5;++i)ht_limb(ht_scene,door+3+i*7,base-46,door+2+i*7,base-2,0,0,231);
    ht_limb(ht_scene,door+11,base-20,door+18,base-19,1,1,250);
    ht_limb(ht_scene,door+9,base-24,door+8,base-14,0,0,151);
    ht_limb(ht_scene,door+7,base-18,door+5,base-16,0,0,177);
    ht_disk(ht_scene,door+29,base-27,2,218);ht_disk(ht_scene,door+29,base-27,1,237);
    ht_limb(ht_scene,door-3,base,door+42,base,2,2,163);
    int shoe=HT_DOOR_SHOE_X-camera;
    ht_door_shoe(shoe,base,true);
    bool straight=g.door_stage>HT_DOOR_STRAIGHTEN ||
        (g.door_stage==HT_DOOR_STRAIGHTEN && g.door_tick>=128);
    ht_door_shoe(shoe+11,base,straight);
    ht_limb(ht_scene,shoe+23,base-1,shoe+32,base+1,2,1,175); /* damp rag */
    ht_limb(ht_scene,shoe+19,base+5,shoe+26,base+5,1,1,195); /* recent print */
    if(character) {
        int x=g.x/256-camera,feet=g.y/256-cy;
        ht_person_pose p=ht_door_pose(&g);
        bool seated=ht_door_seated(&g);
        ht_person_draw(&p);
        if(g.door_stage==HT_DOOR_NOTEBOOK || g.door_stage==HT_DOOR_GRIEF) {
            ht_rect(ht_scene,x+7,feet-7,13,3,165);ht_limb(ht_scene,x+8,feet-9,x+18,feet-9,0,0,240);
            if(g.door_stage==HT_DOOR_GRIEF)ht_limb(ht_scene,x+14,feet-7,x+16,feet-6,0,0,248);
        }
        int carried_x=seated?x+7:p.hand[0].x,carried_y=seated?feet:p.hand[0].y+12;
        ht_door_lamp(carried_x,carried_y);
    }
    ht_render_last.world=ht_render_now()-start;start=ht_render_now();
    ht_world_scale=256; /* The final street has no dramatic vignette. */
    ht_render_last.post=ht_render_now()-start;
}
static void ht_door_caption(const ht_game *g) {
    const char *a="",*b="";
    switch(g->door_stage) {
    case HT_DOOR_YARD: a="THE STEPS ENDED IN A NARROW YARD.";b=ht_abs(g->x/256-HT_DOOR_BARREL)<42?"A: SET DOWN THE LAMP":"";break;
    case HT_DOOR_REST:
        if(g->door_tick<160){a="I BEGAN TO COUNT, CAUGHT MYSELF,";b="AND LISTENED TO THE SPLASH.";}
        else if(g->door_tick<340){a="A LIGHT MOVED ON THE WALL OPPOSITE.";b="IT WAS MY OWN LAMP.";}
        else {a="I WATCHED UNTIL IT SETTLED.";b="THEN I PICKED THE LAMP UP.";}break;
    case HT_DOOR_STREET: a="NO SIGNAL WIRES CROSSED THIS STREET.";b=g->x/256>=HT_DOOR_STOP_X-32?"A: LOWER THE LAMP":"";break;
    case HT_DOOR_SHOES:a="COLD, STILL DAMP AT THE SEAMS.";b="I STAYED CROUCHED FOR A LONG TIME.";break;
    case HT_DOOR_NOTEBOOK:a="I TOOK OUT THE NOTEBOOK.";b="A: WRITE WHAT I HAD DONE";break;
    case HT_DOOR_GRIEF:
        if(g->door_tick<160){a="THE PENCIL POINT BROKE AT THE FULL STOP.";b="THEN I STOPPED.";}
        else if(g->door_tick<320){a="I LET THE WANTING REMAIN THAT SMALL.";}
        else {a="WHEN IT PASSED, I WIPED MY FACE";b="ON THE DRY END OF THE SCARF.";}break;
    case HT_DOOR_STRAIGHTEN:a="I SET IT UPRIGHT BESIDE ITS MATE.";break;
    case HT_DOOR_KNOCK:a="I KNOCKED WITHOUT A PATTERN.";break;
    case HT_DOOR_WAIT:break; /* No end overlay invites an answer or an automatic replay. */
    default:break;
    }
    ht_narration_hash(a);ht_narration_hash(b);
    if(!*a && !*b)return;
    ht_rect(ht_scene,48,230,384,24,245);
    ht_text_color((HT_W-(int)strlen(a)*6)/2,233,a,1,68);
    ht_text_color((HT_W-(int)strlen(b)*6)/2,243,b,1,68);
}
