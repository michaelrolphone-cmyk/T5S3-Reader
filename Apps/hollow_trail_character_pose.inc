/* Instrument-specific poses use world contacts, never elapsed idle time. */
static int ht_ladder_contact(int world_feet,int top,int offset,int side) {
    int relative=world_feet-top-6-offset;
    int rung=ht_floor_div(relative,20),phase=relative-rung*20;
    /* One hand/foot holds while the other travels to its next rung. */
    return top+6+rung*20+(side<0?ht_max(0,phase-10)*2:-10+ht_min(phase,10)*2);
}
static bool ht_character_instrument(int x,int feet,const ht_game *g) {
    const ht_traversal *t=&g->traversal;int cy=g->camera_y/256;
    if(t->mode==HT_TREE) {
        int side=-t->tree_side,phase=(g->stride/512)%12;
        ht_disk(ht_scene,x,feet-25,5,255);
        ht_limb(ht_scene,x,feet-20,x-side*2,feet-9,3,3,255);
        for(int arm=0;arm<2;++arm) {
            int lift=arm?phase:12-phase;
            ht_limb(ht_scene,x,feet-19,x+side*6,feet-24-lift/2,2,1,255);
            ht_limb(ht_scene,x-side*2,feet-9,x+side*6,feet-2-lift/2,2,1,255);
        }
        ht_rect(ht_scene,x+side,feet-25,2,1,30);return true;
    }
    if(t->mode==HT_LADDER) {
        int top,bottom;ht_ladder_ends(g->level,t->ladder,&top,&bottom);
        int world=g->y/256,lean=(world/5)%4-2;
        ht_disk(ht_scene,x+lean/2,feet-25,5,255);
        ht_limb(ht_scene,x+lean/2,feet-20,x,feet-9,3,3,255);
        for(int side=-1;side<=1;side+=2) {
            int hand=ht_clamp(ht_ladder_contact(world,top,23,side),top,bottom)-cy;
            int foot=ht_clamp(ht_ladder_contact(world,top,1,-side),top,bottom)-cy;
            int elbow=feet-16+(hand-(feet-16))/2;
            ht_limb(ht_scene,x+side*2,feet-19,x+side*10,elbow,2,1,255);
            ht_limb(ht_scene,x+side*10,elbow,x+side*6,hand,1,1,255);
            ht_rect(ht_scene,x+side*6-2,hand-1,4,3,255);
            ht_limb(ht_scene,x+side,feet-9,x+side*8,foot-5,2,2,255);
            ht_limb(ht_scene,x+side*8,foot-5,x+side*4,foot,1,1,255);
            ht_rect(ht_scene,x+side*4-2,foot-1,5,2,255);
        }
        ht_rect(ht_scene,x-2+lean/2,feet-25,4,1,70);
        return true;
    }
    if(t->mode==HT_LEDGE || t->mode==HT_CLIMB) {
        const ht_platform *p=&ht_level_land(g->level)[t->ledge];
        int side=t->ledge_side,edge=(side>0?p->left:p->right)-g->camera/256;
        int lip=ht_platform_top(g,t->ledge)-cy,phase=t->climb;
        int shoulder_x=x+side*(phase<8?1:3),shoulder_y=feet-18;
        ht_disk(ht_scene,shoulder_x,feet-25,5,255);
        ht_limb(ht_scene,shoulder_x,feet-21,shoulder_x,shoulder_y,2,2,255);
        ht_limb(ht_scene,shoulder_x,shoulder_y,x-side,feet-8,3,3,255);
        /* Palms stay on the actual lip as the elbows bend, then straighten.
         * The second hand moves inland only once the chest clears the edge. */
        for(int arm=0;arm<2;++arm) {
            int palm=edge+side*(2+arm*4+(phase>9?(phase-9)*2:0));
            int elbow_x=(shoulder_x+palm)/2-side*3;
            int elbow_y=phase<8?lip+5:shoulder_y+5;
            ht_limb(ht_scene,shoulder_x-arm*side,shoulder_y,elbow_x,elbow_y,2,1,255);
            ht_limb(ht_scene,elbow_x,elbow_y,palm,lip,1,1,255);
            ht_rect(ht_scene,palm-2,lip-1,5,2,255);
        }
        if(phase<7) {
            ht_limb(ht_scene,x,feet-9,x-side*5,feet-3,2,1,255);
            ht_limb(ht_scene,x-side*5,feet-3,x-side*2,feet+3,1,1,255);
            ht_limb(ht_scene,x+side,feet-9,edge-side*2,lip+17,2,1,255);
            ht_limb(ht_scene,edge-side*2,lip+17,edge-side*3,lip+23,1,1,255);
        } else {
            int planted=edge+side*(5+(phase-7));
            ht_limb(ht_scene,x,feet-9,planted-side*2,lip-5,2,2,255);
            ht_limb(ht_scene,planted-side*2,lip-5,planted,lip,1,1,255);
            ht_rect(ht_scene,planted-2,lip-2,5,2,255);
            ht_limb(ht_scene,x-side,feet-8,x-side*8,feet-1,2,1,255);
            ht_limb(ht_scene,x-side*8,feet-1,x-side*5,feet+2,1,1,255);
        }
        ht_rect(ht_scene,shoulder_x+side*2,feet-26,2,1,0);
        return true;
    }
    if(t->mode==HT_ROLL || t->mode==HT_CRATE) {
        int object=t->mode==HT_ROLL?t->ball_x:t->crate_x;
        int side=object>=g->x?1:-1;
        int signed_velocity=t->mode==HT_ROLL?t->ball_vx:t->crate_vx;
        int velocity=ht_abs(signed_velocity);
        bool pulling=(velocity>24?signed_velocity:g->facing)*side<0;
        int lean=pulling?-side:side;
        int effort=velocity<32?5:3; /* A stopped load gets a low, planted brace. */
        int shoulder=x+lean*(pulling?6:effort),hip=x-lean*2;
        int hand_x,hand_y;
        if(t->mode==HT_CRATE) {
            hand_x=object/256-g->camera/256-side*HT_CRATE_HALF;
            hand_y=t->crate_y/256-cy-HT_CRATE_HEIGHT+8;
        } else {
            hand_x=object/256-g->camera/256-side*(HT_BALL_RADIUS-2);
            hand_y=t->ball_y/256-cy-4;
        }
        ht_disk(ht_scene,shoulder+side,feet-21,5,255);
        ht_limb(ht_scene,shoulder,feet-16,hip,feet-8,3,3,255);
        int stride=velocity>24?(int)((g->stride/500)%12)-6:0;
        for(int leg=0;leg<2;++leg) {
            int step=leg?-stride:stride;
            int foot=x-lean*(leg?11:0)+(pulling?-step:step)/2;
            ht_limb(ht_scene,hip,feet-8,foot+side*3,feet-5,2,2,255);
            ht_limb(ht_scene,foot+side*3,feet-5,foot,feet,1,1,255);
            ht_rect(ht_scene,foot-2,feet-1,5,2,255);
        }
        for(int arm=0;arm<2;++arm) {
            int elbow=pulling?(shoulder+hand_x)/2:x+side*7,ey=feet-(pulling?17:12)+arm*3;
            ht_limb(ht_scene,shoulder,feet-16+arm*2,elbow,ey,2,1,255);
            ht_limb(ht_scene,elbow,ey,hand_x,hand_y+arm*4,1,1,255);
            ht_disk(ht_scene,hand_x,hand_y+arm*4,1,255);
        }
        ht_rect(ht_scene,shoulder+side*3,feet-22,2,1,0);
        return true;
    }
    return false;
}
