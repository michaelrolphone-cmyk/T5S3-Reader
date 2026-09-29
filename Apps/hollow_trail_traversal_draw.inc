#include "hollow_trail_geography_draw.inc"
#include "hollow_trail_terrain_draw.inc"
/* Dynamic solids are drawn at logical resolution, outside the blurred scenery
 * cache. One frozen ht_game supplies camera, object poses, collision and art. */
static void ht_draw_traversal(const ht_game *g) {
    int cx=g->camera/256,cy=g->camera_y/256;
    const ht_platform *land=ht_level_land(g->level);
    const ht_mechanics *m=ht_mech(g);
    ht_scene_geography(g);
    if(m->water_right) {
        int water=m->boat_deck+10-cy;
        ht_rect(ht_scene,m->water_left-cx,water,m->water_right-m->water_left,HT_H,110);
        for(int band=0;band<4;++band)
            ht_rect(ht_scene,m->water_left-cx,water+band*3,m->water_right-m->water_left,3,55+band*14);
        for(int x=m->water_left+4;x<m->water_right;x+=27) {
            int wave=(int)((g->ticks/3+(unsigned)x)%7);
            ht_rect(ht_scene,x-cx,water+wave,14,1,35);
        }
        for(int shore=0;shore<2;++shore) {
            int x=(shore?m->water_right:m->water_left)-cx;
            ht_rect(ht_scene,x-2,water-18,4,32,255);
            ht_limb(ht_scene,x-9,water-15,x+9,water-15,1,1,255);
            for(int reflection=0;reflection<3;++reflection)
                ht_rect(ht_scene,x-3+reflection%2,water+3+reflection*4,5,2,160-reflection*20);
            if(g->level==4) for(int reed=0;reed<5;++reed) {
                int rx=x+(shore?-1:1)*(8+reed*4);
                ht_limb(ht_scene,rx,water+3,rx-2,water-16-reed%3*5,0,0,190);
                ht_limb(ht_scene,rx,water-3,rx+5,water-10,0,0,150);
            }
        }
    }
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&land[i];
        for(int part=0;part<3;++part) {
            int l,r,top;if(!ht_platform_piece(g,i,part,&l,&r,&top)) break;
            int x=l-cx,y=top-cy,w=r-l;
            if(x>=HT_W+ht_view_margin() || x+w< -ht_view_margin()) continue;
            ht_draw_land(g,i,part,l,r,top);
            if(i==m->bridge) {
                ht_limb(ht_scene,x+5,y,x+5,p->top-32*HT_BRIDGE_STEP-32-cy,0,0,200);
                ht_limb(ht_scene,x+w-5,y,x+w-5,p->top-32*HT_BRIDGE_STEP-32-cy,0,0,200);
            }
        }
    }
    if(m->plate_kind) {
        int x=m->plate_x-cx,y=m->plate_y-cy;
        ht_rect(ht_scene,x-21,y-1,42,3,255);
        ht_rect(ht_scene,x-17,y-(ht_plate_weighted(g)?1:3),34,2,100);
        ht_disk(ht_scene,x-19,y,2,255);ht_disk(ht_scene,x+19,y,2,255);
        const ht_platform *bridge=&land[m->bridge];
        int pulley=bridge->left-cx-6;
        ht_limb(ht_scene,x+21,y+4,pulley,y+4,0,0,130);
        ht_limb(ht_scene,pulley,y+4,pulley,bridge->top-32*HT_BRIDGE_STEP-32-cy,0,0,150);
        ht_disk(ht_scene,pulley,bridge->top-32*HT_BRIDGE_STEP-32-cy,5,210);
        ht_disk(ht_scene,pulley,bridge->top-32*HT_BRIDGE_STEP-32-cy,2,35);
        ht_limb(ht_scene,pulley,bridge->top-32*HT_BRIDGE_STEP-32-cy,bridge->left+5-cx,bridge->top-32*HT_BRIDGE_STEP-32-cy,0,0,200);
        /* A closing pawl visibly locks the lowered span. */
        if(g->traversal.bridge_open==32) ht_rect(ht_scene,pulley-4,y-3,8,3,255);
    }
    /* Distinct natural silhouettes stay anchored to the new physical route. */
    for(int i=0;i<HT_PLATFORMS;++i) {
        if(i==m->bridge || (i!=0 && i!=2 && i!=5 && i!=8)) continue;
        const ht_platform *p=&land[i];
        int world=i==0?15:p->left+(p->right-p->left)*2/3;
        int x=world-cx,y=ht_surface_at(g,i,world)-cy;
        if(x< -160-ht_view_margin() || x>HT_W+160+ht_view_margin()) continue;
        if(g->level==2) ht_cactus_on(ht_scene,x,y,i==1?39:84,255);
        else if(g->level==0 || g->level==4 || g->level==6 || g->level==8)
            ht_tree(ht_scene,x,y,g->level==0?340+(i%3)*65:130+(i%3)*27,g->level==0?23+(i%3)*5:7,(unsigned)i+901,255);
        else {
            ht_rect(ht_scene,x,y-42,3,42,255);ht_rect(ht_scene,x-7,y-44,17,3,255);
            ht_rect(ht_scene,x-4,y-39,11,13,170);
        }
    }
    ht_story_landmarks(g);
    for(unsigned i=0;i<3;++i) {
        if(!m->ladders[i].x) continue;
        int top,bottom;ht_ladder_ends(g->level,i,&top,&bottom);
        int x=m->ladders[i].x-cx;
        if(x< -12 || x>HT_W+12) continue;
        ht_rect(ht_scene,x-9,top-cy,3,bottom-top,255);
        ht_rect(ht_scene,x+6,top-cy,3,bottom-top,255);
        for(int y=top+6;y<bottom;y+=10) ht_rect(ht_scene,x-7,y-cy,14,3,255);
    }
    if(m->ball_x) {
        int bx=g->traversal.ball_x/256-cx,by=g->traversal.ball_y/256-cy;
        if(bx>=-18 && bx<=HT_W+18 && by>=-18 && by<=HT_H+18) {
            ht_disk(ht_scene,bx,by,HT_BALL_RADIUS,255);
            ht_disk(ht_scene,bx-1,by-1,HT_BALL_RADIUS-2,165);
            ht_triangle(ht_scene,bx-11,by-8,bx+1,by-13,bx-3,by+2,80);
            ht_triangle(ht_scene,bx+10,by-8,bx+12,by+9,bx-1,by+13,225);
            /* Fractures rotate by travelled arc length: no sliding texture. */
            static const int16_t sine[32]={0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50};
            unsigned angle=(unsigned)(g->traversal.ball_roll/(HT_BALL_RADIUS*50))&31u;
            int sn=sine[angle],cs=sine[(angle+8)&31u];
            int ax=(-9*cs+5*sn)/256,ay=(-9*sn-5*cs)/256;
            int bx2=(7*cs+2*sn)/256,by2=(7*sn-2*cs)/256;
            ht_limb(ht_scene,bx+ax,by+ay,bx+2,by+2,0,0,255);
            ht_limb(ht_scene,bx+2,by+2,bx+bx2,by+by2,0,0,255);
            ht_disk(ht_scene,bx+(4*cs-8*sn)/256,by+(4*sn+8*cs)/256,2,210);
        }
    }
    if(m->crate_x) {
        int x=g->traversal.crate_x/256-cx,y=g->traversal.crate_y/256-HT_CRATE_HEIGHT-cy;
        ht_rect(ht_scene,x-12,y,24,26,255);ht_rect(ht_scene,x-10,y+2,19,22,150);
        for(int dy=7;dy<24;dy+=6) ht_rect(ht_scene,x-10,y+dy,19,1,235);
        ht_rect(ht_scene,x+7,y+2,3,22,210);
        ht_limb(ht_scene,x-9,y+22,x+6,y+3,2,2,255);
        ht_limb(ht_scene,x-7,y+22,x+8,y+3,0,0,75);
        for(int dx=-8;dx<=8;dx+=16) {ht_rect(ht_scene,x+dx,y+3,1,1,0);ht_rect(ht_scene,x+dx,y+22,1,1,0);}
    }
    if(m->boat_right) {
        int x=g->traversal.boat_x/256-cx,y=ht_boat_top(g)/256-cy;
        /* Raised gunwales, pointed bow, ribs, seats and a working oar. */
        for(int dy=0;dy<15;++dy) ht_rect(ht_scene,x-29+dy/2,y+dy,58-dy,1,dy==0?255:dy<9?190:255);
        ht_limb(ht_scene,x-29,y-4,x-21,y+8,1,1,255);
        ht_limb(ht_scene,x+29,y-5,x+21,y+8,1,1,255);
        ht_rect(ht_scene,x-23,y-1,46,2,255);
        for(int rib=-16;rib<=16;rib+=8) ht_rect(ht_scene,x+rib,y+3,1,7,235);
        ht_rect(ht_scene,x-13,y,5,3,70);ht_rect(ht_scene,x-4,y,8,3,70);ht_rect(ht_scene,x+10,y,5,3,70);
        int stroke=ht_boat_stroke(g);
        ht_limb(ht_scene,x+5+stroke/2,y-6,x+27+stroke,y+12,0,0,255);
        ht_limb(ht_scene,x+27+stroke,y+12,x+32+stroke,y+15,2,1,255);
        ht_rect(ht_scene,x-20,y+15,40,1,50);
        int speed=ht_abs(g->traversal.boat_vx)/64;
        for(int i=0;i<speed;++i) {
            int side=g->traversal.boat_vx>0?-1:1;
            int at=x+side*(31+i*6),rip=(int)((g->ticks+i*3)%8);
            ht_rect(ht_scene,at,y+13+rip/3,4+i,1,45);
        }
    }
    if(m->rope_length) {
        int ax=m->rope_anchor_x-cx,ay=m->rope_anchor_y-cy;
        int foot=ay+108;
        ht_limb(ht_scene,ax-43,foot,ax-43,ay-14,3,2,255);
        ht_limb(ht_scene,ax-45,ay-12,ax+10,ay-12,3,2,255);
        ht_limb(ht_scene,ax-42,ay+15,ax-12,ay-12,1,1,255);
        ht_limb(ht_scene,ax,ay-12,ax,ay-3,0,0,255);
        ht_disk(ht_scene,ax,ay-3,4,255);ht_disk(ht_scene,ax,ay-3,2,0);
        for(int i=1;i<HT_ROPE_NODES;++i) {
            int x0=g->traversal.rope_px[i-1]/256-cx,y0=g->traversal.rope_py[i-1]/256-cy;
            int x1=g->traversal.rope_px[i]/256-cx,y1=g->traversal.rope_py[i]/256-cy;
            ht_limb(ht_scene,x0,y0,x1,y1,0,0,255);
            if(i==6) ht_disk(ht_scene,x1,y1,1,255);
        }
    }
    ht_contact_detail(g);
    int x,y;
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
    case HT_ROLL: text=g->traversal.mode?"LEFT / RIGHT ROLL - A RELEASE":"A GRIP STONE - ROLL ONTO THE WEIGHT PLATE";break;
    case HT_CRATE: text=g->traversal.mode?"LEFT / RIGHT PUSH - A RELEASE - B JUMP":"A GRIP CRATE - PUSH OR PULL WITH L/R";break;
    case HT_LADDER: text=g->traversal.mode?"UP / DOWN CLIMB - B JUMP OFF":"UP / DOWN TO CLIMB - B JUMP";break;
    case HT_ROPE: text=g->traversal.mode?"LEFT / RIGHT PUMP - B LEAP - A LET GO":"A GRAB ROPE - SWING ACROSS THE RAVINE";break;
    case HT_BOAT: text=g->traversal.mode?"LEFT / RIGHT ROW - A RELEASE - B JUMP":"A BOARD BOAT - ROW ACROSS THE LAKE";break;
    default: break;
    }
    if(text) {ht_rect(ht_scene,66,33,348,19,255);ht_text_color(74,38,text,1,0);}
}
