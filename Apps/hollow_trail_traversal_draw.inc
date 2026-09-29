/* Dynamic solids are drawn at logical resolution, outside the blurred scenery
 * cache. One frozen ht_game supplies camera, object poses, collision and art. */
static void ht_draw_traversal(const ht_game *g) {
    int cx=g->camera/256,cy=g->camera_y/256;
    const ht_platform *land=ht_level_land(g->level);
    int ground=land[0].top;
    /* The lake is an actual hazard; its banks and boat use the same y datum. */
    ht_rect(ht_scene,1790-cx,ground+13-cy,320,HT_H,110);
    for(int x=1800;x<2110;x+=30) ht_rect(ht_scene,x-cx,ground+16-cy,16,2,30);
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&land[i];int x=p->left-cx,y=p->top-cy,w=p->right-p->left;
        bool bank=i==0 || i==1 || i==5 || i==6 || i==9;
        if(x>=HT_W || x+w<0 || y>=HT_H || (!bank && y+24<0)) continue;
        ht_rect(ht_scene,x,y,w,bank?HT_H-y:18,255);
        ht_rect(ht_scene,x,y+3,w,2,170);
        /* World-anchored surface details identify industrial versus natural decks. */
        for(int at=p->left+8;at<p->right;at+=24) {
            if(g->level==0 || g->level==4 || g->level==8)
                ht_limb(ht_scene,at-cx,y+14,at+7-cx,y+22,2,0,255);
            else ht_rect(ht_scene,at-cx,y+7,3,8,110);
        }
    }
    /* Distinct natural silhouettes stay anchored to the new physical route. */
    for(int i=0;i<HT_PLATFORMS;++i) {
        if(i!=0 && i!=1 && i!=4 && i!=8) continue;
        const ht_platform *p=&land[i];
        int x=(i==0?15:p->left+64)-cx,y=p->top-cy;
        if(x< -80 || x>HT_W+80) continue;
        if(g->level==2) ht_cactus_on(ht_scene,x,y,i==1?39:84,255);
        else if(g->level==0 || g->level==4 || g->level==6 || g->level==8)
            ht_tree(ht_scene,x,y,i==0?250:100,4,(unsigned)i+901,255);
        else {
            ht_rect(ht_scene,x,y-42,3,42,255);ht_rect(ht_scene,x-7,y-44,17,3,255);
            ht_rect(ht_scene,x-4,y-39,11,13,170);
        }
    }
    for(unsigned i=0;i<3;++i) {
        int top,bottom;ht_ladder_ends(g->level,i,&top,&bottom);
        int x=ht_ladder_x(i)-cx;
        if(x< -12 || x>HT_W+12) continue;
        ht_rect(ht_scene,x-9,top-cy,3,bottom-top,255);
        ht_rect(ht_scene,x+6,top-cy,3,bottom-top,255);
        for(int y=top+6;y<bottom;y+=10) ht_rect(ht_scene,x-7,y-cy,14,3,255);
    }
    int bx=g->traversal.ball_x/256-cx,by=g->traversal.ball_y/256-cy;
    if(bx>=-50 && bx<=HT_W+50 && by>=-50 && by<=HT_H+50) {
        ht_disk(ht_scene,bx,by,48,255);
        ht_disk(ht_scene,bx,by,40,190);
        if(g->level==0 || g->level==5 || g->level==8) {
            ht_limb(ht_scene,bx-24,by-17,bx+7,by+23,3,1,255);
            ht_limb(ht_scene,bx+7,by+23,bx+25,by+8,1,1,255);
        } else {
            ht_rect(ht_scene,bx-32,by-20,64,4,255);
            ht_rect(ht_scene,bx-32,by+16,64,4,255);
        }
    }
    int x=g->traversal.crate_x/256-cx,y=ground-64-cy;
    ht_rect(ht_scene,x-20,y,40,64,255);ht_rect(ht_scene,x-16,y+4,32,56,90);
    ht_limb(ht_scene,x-16,y+4,x+16,y+60,2,2,255);
    ht_limb(ht_scene,x+16,y+4,x-16,y+60,2,2,255);
    x=g->traversal.boat_x/256-cx;y=ground-cy;
    ht_rect(ht_scene,x-44,y,88,5,255);
    ht_triangle(ht_scene,x-44,y+4,x+44,y+4,x+29,y+19,255);
    ht_triangle(ht_scene,x-44,y+4,x+29,y+19,x-28,y+19,255);
    ht_rect(ht_scene,x-26,y+7,51,3,170);
    int rx,ry;ht_rope_point(g,&rx,&ry);
    int ax=1285-cx,ay=land[3].top-155-cy;
    ht_limb(ht_scene,ax-24,ay-6,ax+24,ay-6,4,4,255);
    ht_limb(ht_scene,ax,ay,rx-cx,ry-20-cy,2,2,255);
    ht_disk(ht_scene,rx-cx,ry-20-cy,4,255);
    /* Exit architecture follows its actual terrace when the camera climbs. */
    x=HT_GOAL-cx;y=land[9].top-cy;
    ht_rect(ht_scene,x-20,y-52,6,52,255);ht_rect(ht_scene,x+14,y-52,6,52,255);
    ht_rect(ht_scene,x-22,y-56,44,6,255);
}
static void ht_traversal_prompt(const ht_game *g) {
    const char *text=NULL;
    switch(ht_traversal_near(g)) {
    case HT_LEDGE: text="UP / A CLIMB - DOWN DROP - B JUMP AWAY";break;
    case HT_CLIMB: text="CLIMBING";break;
    case HT_ROLL: text=g->traversal.mode?"LEFT / RIGHT ROLL - A RELEASE":"A GRAB THE STONE / DRUM - FILL THE GAP";break;
    case HT_CRATE: text=g->traversal.mode?"LEFT / RIGHT PUSH - A RELEASE - B JUMP":"A GRAB CRATE - PUSH BELOW THE HIGH LEDGE";break;
    case HT_LADDER: text="UP / DOWN CLIMB - B JUMP OFF";break;
    case HT_ROPE: text=g->traversal.mode?"LEFT / RIGHT PUMP - B LEAP - A LET GO":"A GRAB ROPE - SWING ACROSS THE RAVINE";break;
    case HT_BOAT: text=g->traversal.mode?"LEFT / RIGHT ROW - A RELEASE - B JUMP":"A BOARD BOAT - ROW ACROSS THE LAKE";break;
    default: break;
    }
    if(text) {ht_rect(ht_scene,66,33,348,19,255);ht_text_color(74,38,text,1,0);}
}
