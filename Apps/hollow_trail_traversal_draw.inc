#include "hollow_trail_material_detail.inc"
#include "hollow_trail_geography_draw.inc"
#include "hollow_trail_terrain_draw.inc"
#include "hollow_trail_landscape_detail.inc"
#include "hollow_trail_forest_draw.inc"
#include "hollow_trail_city_draw.inc"
#include "hollow_trail_chapter_draw.inc"
/* Continuous dark rope with alternating diagonal fibre ridges. The pattern
 * belongs to arc length, so it turns with the rope rather than screen time. */
static void ht_twisted_rope(int x,int y,int ex,int ey,int *along) {
    int dx=ex-x,dy=ey-y,len=ht_max(1,(int)ht_isqrt((unsigned)(dx*dx+dy*dy)));
    ht_limb(ht_scene,x,y,ex,ey,1,1,250);
    int nx=-dy*256/len,ny=dx*256/len;
    for(int d=0;d<len;++d)if((d+*along)%4==0) {
        int px=x+dx*d/len,py=y+dy*d/len;
        ht_limb(ht_scene,px-nx/256,py-ny/256,
            px+nx/256+dx*2/len,py+ny/256+dy*2/len,0,0,145);
    }
    *along+=len;
}
/* Dynamic solids are drawn at logical resolution, outside the blurred scenery
 * cache. One frozen ht_game supplies camera, object poses, collision and art. */
static void ht_draw_traversal(const ht_game *g) {
    int cx=g->camera/256,cy=g->camera_y/256;
    const ht_platform *land=ht_level_land(g->level);
    const ht_mechanics *m=ht_mech(g);
    if(g->level==0)ht_forest_backdrop(g);
    if(g->level==1)ht_city_backdrop(g);
    ht_authored_backdrop(g);
    if(!ht_grotto_opaque(g)) {
        ht_scene_geography(g);
        ht_landscape_detail(g);
        if(m->water_right) {
            int water=m->boat_deck+10-cy;
            int left=g->level==4?cx-ht_view_margin():m->water_left;
            int right=g->level==4?cx+HT_W+ht_view_margin():m->water_right;
            ht_rect(ht_scene,left-cx,water,right-left,HT_H,110);
            for(int band=0;band<16;++band)
                ht_rect(ht_scene,left-cx,water+band*3,right-left,3,35+band*5);
            for(int x=m->water_left+4;x<m->water_right;x+=27) {
                int wave=(int)((g->ticks/3+(unsigned)x)%7);
                ht_rect(ht_scene,x-cx,water+wave,14,1,35);
            }
            for(int shore=0;shore<2;++shore) {
                int x=(shore?m->water_right:m->water_left)-cx;
                if(g->level!=4) {
                    ht_rect(ht_scene,x-2,water-18,4,32,255);
                    ht_limb(ht_scene,x-9,water-15,x+9,water-15,1,1,255);
                }
                for(int reflection=0;reflection<3;++reflection)
                    ht_rect(ht_scene,x-3+reflection%2,water+3+reflection*4,5,2,160-reflection*20);
                if(g->level==4) for(int reed=0;reed<5;++reed) {
                    int rx=x+(shore?-1:1)*(8+reed*4);
                    ht_limb(ht_scene,rx,water+3,rx-2,water-16-reed%3*5,0,0,190);
                    ht_limb(ht_scene,rx,water-3,rx+5,water-10,0,0,150);
                }
            }
        }
    }
    ht_dam_spray(g);
    ht_boat_grotto(g);
    /* Background/grotto retain the original full 960x540 native path.
     * From here forward, native A/B uses 960x270 detail: full horizontal
     * resolution with one computed row duplicated to each physical row pair. */
    if(ht_native_active)ht_native_foreground_half_y=true;
    /* Distinct natural silhouettes stay anchored to the new physical route. */
    for(int i=0;i<HT_PLATFORMS;++i) {
        if(g->level==1 || i==m->bridge || (i!=0 && i!=2 && i!=5 && i!=8) || (g->level==4 && i==0)) continue;
        int world=ht_planted_tree_x(g->level,i);
        int x=world-cx,y=ht_surface_at(g,i,world)-cy;
        if(x< -160-ht_view_margin() || x>HT_W+160+ht_view_margin()) continue;
        if(g->level==2) ht_detail_cactus(x,y,i==1?39:84,(unsigned)i+901);
        else if(g->level==0 || g->level==4 || g->level==6 || g->level==8)
            ht_grounded_tree(g,i,world,g->level==0?340+(i%3)*65:130+(i%3)*27,g->level==0?23+(i%3)*5:7,(unsigned)i+901);

    }
    ht_story_landmarks(g);
    ht_draw_evidence(g);
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&land[i];
        for(int part=0;part<3;++part) {
            int l,r,top;if(!ht_platform_piece(g,i,part,&l,&r,&top)) break;
            int x=l-cx,y=top-cy,w=r-l;
            if(x>=HT_W+ht_view_margin() || x+w< -ht_view_margin()) continue;
            if(g->level==0 && i==1)continue; /* actual fallen trunk is drawn below */
            ht_draw_land(g,i,part,l,r,top);
            if(i==m->bridge) {
                ht_limb(ht_scene,x+5,y,x+5,p->top-32*HT_BRIDGE_STEP-32-cy,0,0,200);
                ht_limb(ht_scene,x+w-5,y,x+w-5,p->top-32*HT_BRIDGE_STEP-32-cy,0,0,200);
            }
        }
    }
    if(g->level==0)ht_forest_log_draw(g);
    ht_city_roof_detail(g);
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
    for(unsigned i=0;i<3;++i) {
        if(!m->ladders[i].x) continue;
        int top,bottom;ht_ladder_ends(g->level,i,&top,&bottom);
        int x=m->ladders[i].x-cx;
        int ladder_margin=ht_authored_chapter(g->level)?ht_view_margin():0;
        if(x< -12-ladder_margin || x>HT_W+12+ladder_margin) continue;
        if(g->level==8) {ht_alpine_line(x,top-cy,bottom-cy);continue;}
        ht_rect(ht_scene,x-9,top-cy,3,bottom-top,255);
        ht_rect(ht_scene,x+6,top-cy,3,bottom-top,255);
        for(int y=top+6;y<bottom;y+=10) ht_rect(ht_scene,x-7,y-cy,14,3,255);
    }
    if(m->ball_x) {
        int bx=g->traversal.ball_x/256-cx,by=g->traversal.ball_y/256-cy;
        if(bx>=-18 && bx<=HT_W+18 && by>=-18 && by<=HT_H+18) {
            if(g->level==0) {
                /* Rounded weathering is still the physical circular stone;
                 * shallow contour chips and broad facets replace the wheel rim. */
                static const int8_t rim[24][2]={{16,0},{15,4},{14,8},{11,11},{8,14},{4,15},
                    {0,16},{-4,15},{-8,13},{-11,11},{-14,8},{-15,4},
                    {-16,0},{-15,-4},{-14,-8},{-11,-11},{-8,-14},{-4,-15},
                    {0,-16},{4,-15},{8,-14},{11,-11},{14,-8},{15,-4}};
                for(int v=0;v<24;++v) {
                    int n=(v+1)%24;
                    ht_triangle(ht_scene,bx-2,by-3,bx+rim[v][0],by+rim[v][1],
                        bx+rim[n][0],by+rim[n][1],v<8?219:(v<15?180:148));
                }
                ht_limb(ht_scene,bx-10,by-7,bx-6,by-12,0,0,110);
            } else {
            ht_disk(ht_scene,bx,by,HT_BALL_RADIUS,255);
            ht_disk(ht_scene,bx-1,by-1,HT_BALL_RADIUS-2,165);
            ht_triangle(ht_scene,bx-11,by-8,bx+1,by-13,bx-3,by+2,80);
            ht_triangle(ht_scene,bx+10,by-8,bx+12,by+9,bx-1,by+13,225);
            }
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
        int half=ht_boat_half(g);
        if(g->level==4) {
            /* A single curved gunwale and a deep, solid wooden hull. Sparse
             * broad shapes survive half-resolution e-paper sampling. */
            for(int dy=0;dy<17;++dy) {
                int taper=dy*dy/22;
                ht_rect(ht_scene,x-half+taper,y+dy,2*(half-taper),1,dy<12?246:255);
            }
            ht_limb(ht_scene,x-half,y-6,x-half+11,y+12,2,2,255);
            ht_limb(ht_scene,x+half-1,y-5,x+half-9,y+12,2,2,255);
            ht_limb(ht_scene,x-half+5,y-2,x-4,y+2,2,1,255);
            ht_limb(ht_scene,x-4,y+2,x+half-3,y-2,1,2,255);
            ht_limb(ht_scene,x-half+16,y+9,x+half-13,y+10,0,0,210);
            int mast=x-8,head=y-70;
            ht_limb(ht_scene,mast,y+1,mast+4,head,2,1,255);
            /* One continuous forestay, hanging in a shallow curve. */
            ht_limb(ht_scene,mast+3,head+12,mast-7,y-31,1,1,230);
            ht_limb(ht_scene,mast-7,y-31,x-half+17,y-1,1,1,230);
            if(g->traversal.mode==HT_BOAT) {
                int stroke=ht_boat_stroke(g);
                ht_limb(ht_scene,x+5+stroke/2,y-6,x+27+stroke,y+12,1,1,255);
                ht_limb(ht_scene,x+27+stroke,y+12,x+32+stroke,y+15,2,1,255);
            }
            int bank=m->water_left-cx;
            if(g->traversal.boat_x/256-half-m->water_left<=3) {
                int ledge=ht_surface_at(g,0,m->water_left-4)-cy+8;
                ht_limb(ht_scene,bank-4,ledge,x-half+2,y-4,1,1,235);
            }
        } else {
            for(int dy=0;dy<15;++dy) {
                int taper=dy*dy/24;
                ht_rect(ht_scene,x-half+1+taper,y+dy,2*(half-1-taper),1,dy==0?255:dy<9?220:255);
            }
            ht_limb(ht_scene,x-half+1,y-4,x-half+9,y+8,1,1,255);
            ht_limb(ht_scene,x+half-1,y-5,x+half-9,y+8,1,1,255);
            ht_rect(ht_scene,x-half+7,y-1,2*half-14,2,255);
            for(int rib=-16;rib<=16;rib+=8) ht_rect(ht_scene,x+rib,y+3,1,7,235);
            int stroke=ht_boat_stroke(g);
            ht_limb(ht_scene,x+5+stroke/2,y-6,x+27+stroke,y+12,0,0,255);
            ht_limb(ht_scene,x+27+stroke,y+12,x+32+stroke,y+15,2,1,255);
        }
        /* Reflection dissolves in horizontal mist strips, never a black
         * inverted copy competing with the small hull silhouette. */
        for(int band=0;g->level!=4 && band<4;++band)
            ht_rect(ht_scene,x-half+7+band*3,y+16+band*3,2*half-14-band*6,1,90-band*15);
        int speed=ht_abs(g->traversal.boat_vx)/64;
        for(int i=0;i<speed;++i) {
            int side=g->traversal.boat_vx>0?-1:1;
            int at=x+side*(half+1+i*6),rip=(int)((g->ticks+i*3)%8);
            ht_rect(ht_scene,at,y+13+rip/3,4+i,1,45);
        }
    }
    if(m->rope_length) {
        int rest_x=m->rope_anchor_x-cx,rest_y=m->rope_anchor_y-cy;
        int ax=g->traversal.rope_px[0]/256-cx,ay=g->traversal.rope_py[0]/256-cy;
        if(g->level==0) {
            int trunk=m->rope_anchor_x-96;
            ht_grounded_tree(g,4,trunk,295,16,1917);
            ht_tree_branch limb=ht_rope_branch(g,trunk);
            ht_limb(ht_scene,limb.x-cx,limb.y-cy,limb.mx-cx,limb.my-cy,9,6,255);
            ht_limb(ht_scene,limb.mx-cx,limb.my-cy,limb.ex-cx,limb.ey-cy,6,2,255);
            ht_limb(ht_scene,ax-34,ay-18,ax-15,ay-42,3,0,255);
            ht_leaf_cluster(ht_scene,ax+16,ay-14,17,2917,255);
        } else {
        int foot=rest_y+108;
        ht_limb(ht_scene,rest_x-43,foot,rest_x-43,rest_y-14,3,2,255);
        ht_limb(ht_scene,rest_x-45,rest_y-12,ax+10,ay-12,3,2,255);
        ht_limb(ht_scene,rest_x-42,rest_y+15,rest_x-12,rest_y-12,1,1,255);
        }
        /* Two wraps over the support, a compact hitch and a frayed free tail.
         * No white hole or floating anchor marker. */
        for(int wrap=0;wrap<2;++wrap) {
            int wx=ax-3+wrap*4;
            ht_limb(ht_scene,wx,ay-18,wx+2,ay-9,1,1,135);
            ht_limb(ht_scene,wx+2,ay-9,ax,ay-3,1,1,240);
        }
        ht_limb(ht_scene,ax-2,ay-5,ax+2,ay-1,2,1,245);
        ht_limb(ht_scene,ax,ay-5,ax-1,ay,0,0,140);
        ht_limb(ht_scene,ax,ay-1,ax,ay+2,1,1,250);
        ht_limb(ht_scene,ax+2,ay-3,ax+6,ay+5,1,0,235);
        ht_limb(ht_scene,ax+6,ay+5,ax+8,ay+7,0,0,210);
        int along=0;
        for(int i=1;i<HT_ROPE_NODES;++i) {
            int x0=g->traversal.rope_px[i-1]/256-cx,y0=g->traversal.rope_py[i-1]/256-cy;
            int x1=g->traversal.rope_px[i]/256-cx,y1=g->traversal.rope_py[i]/256-cy;
            ht_twisted_rope(x0,y0,x1,y1,&along);
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
    if(g->level==0)return; /* The forest teaches through visible physical consequences. */
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
