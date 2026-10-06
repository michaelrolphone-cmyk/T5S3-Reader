#include "hollow_trail_first_house_draw.inc"
#include "hollow_trail_sleep_draw.inc"
#include "hollow_trail_pouch_draw.inc"
#include "hollow_trail_grip_draw.inc"
#include "hollow_trail_material_detail.inc"
#include "hollow_trail_waiting_draw.inc"
#include "hollow_trail_distribution_draw.inc"
#include "hollow_trail_station_draw.inc"
#include "hollow_trail_carriage_draw.inc"
#include "hollow_trail_signal_cabin_draw.inc"
#include "hollow_trail_living_bed_draw.inc"
#include "hollow_trail_glasshouse_draw.inc"
#include "hollow_trail_food_draw.inc"
#include "hollow_trail_care_draw.inc"
#include "hollow_trail_counts_draw.inc"
#include "hollow_trail_drawing_draw.inc"
#include "hollow_trail_quarry_ascent_draw.inc"
#include "hollow_trail_trestle_draw.inc"
#include "hollow_trail_geography_draw.inc"
#include "hollow_trail_terrain_draw.inc"
#include "hollow_trail_landscape_detail.inc"
#include "hollow_trail_forest_draw.inc"
#include "hollow_trail_city_draw.inc"
#include "hollow_trail_chapter_draw.inc"
#include "hollow_trail_stove_draw.inc"
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
    ht_forest_city_horizon(g);
    if(g->level==1)ht_city_backdrop(g);
    ht_city_vanished_house(g);
    ht_authored_backdrop(g);
    ht_quarry_country(g);
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
    ht_gallery_wall(g);
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
    ht_forest_mill(g);
    ht_city_schoolroom(g);
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
    ht_city_service_arrival(g);
    ht_city_rain_tank(g);
    ht_city_signal_room(g);
    ht_stove_room(g,0,0);
    ht_station_room(g);
    ht_distribution_room(g,48);
    ht_distribution_service_line(g);
    ht_sleep_room(g,g->sleep_view_active?ht_sleep_closure(g->sleep_view_stage,g->sleep_view_tick):0,
      !g->sleep_view_active || g->sleep_view_stage==0 || g->sleep_view_stage==4 || (g->sleep_view_stage==1 && g->sleep_view_tick<80) || (g->sleep_view_stage==3 && g->sleep_view_tick>=96));
    ht_gallery_floor(g);
    ht_living_bed(g);
    ht_glasshouse_arrival(g);
    ht_food_room(g);
    ht_counts_room(g);
    ht_drawing_room(g);
    ht_cabinet_room(g);
    ht_first_house_room(g,g->first_house_lean);
    ht_pouch_room(g);ht_grip_room(g);
    ht_quarry_ascent_traces(g);
    ht_waiting_room(g,g->waiting_view_active?(unsigned)ht_waiting_pull(g->waiting_view_tick):0,
        g->waiting_view_active && g->waiting_view_tick>=104?ht_waiting_swing(g->waiting_view_tick-104):0);
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
            /* A chipped mineral mass, not a wheel. Keep the same stable
             * circular contact envelope so existing roll/step physics agree.
             * Bright upper facet and dark rim survive either dark soil or fog. */
            static const int8_t rim[12][2]={{16,0},{13,7},{10,12},{1,16},
                {-7,13},{-14,7},{-16,-1},{-12,-10},{-5,-15},{3,-16},{12,-10},{15,-4}};
            ht_limb(ht_scene,bx-14,by+16,bx+17,by+16,1,0,240);
            for(int v=0;v<12;++v) {
                int n=(v+1)%12;
                ht_triangle(ht_scene,bx-3,by-3,bx+rim[v][0],by+rim[v][1],
                    bx+rim[n][0],by+rim[n][1],v<5?224:(v<8?166:62));
                ht_limb(ht_scene,bx+rim[v][0],by+rim[v][1],
                    bx+rim[n][0],by+rim[n][1],0,0,v>=7?24:255);
            }
            ht_triangle(ht_scene,bx-11,by-9,bx+2,by-14,bx-3,by-3,26);
            ht_triangle(ht_scene,bx-3,by-3,bx+12,by-8,bx+8,by+9,120);
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
        int x=g->traversal.crate_x/256-cx,y=g->traversal.crate_y/256-ht_crate_height(g->level)-cy;
        if(g->level==0) {
            /* Sloping writing board over a level timber frame. The frame is
             * the same 24x14 physical body; four legs meet the real contour. */
            bool lit=ht_mill_register_lit(g);int floor=g->traversal.crate_y/256-cy;
            ht_limb(ht_scene,x-12,y,x+12,y,1,1,220);
            ht_triangle(ht_scene,x-14,y-2,x+12,y-5,x+14,y,lit?104:206);
            ht_triangle(ht_scene,x-14,y-2,x+14,y,x-12,y+2,lit?104:206);
            ht_limb(ht_scene,x-11,y+1,x-10,ht_surface_at(g,3,g->traversal.crate_x/256-10)-cy,1,1,231);
            ht_limb(ht_scene,x+10,y,x+9,ht_surface_at(g,3,g->traversal.crate_x/256+9)-cy,1,1,227);
            ht_limb(ht_scene,x-10,floor-8,x+9,floor-8,1,1,203);
            ht_triangle(ht_scene,x-9,y-3,x+8,y-5,x+10,y-1,lit?38:157);
            ht_triangle(ht_scene,x-9,y-3,x+10,y-1,x-7,y+1,lit?38:157);
            /* The ruled column and seven entries emerge into the hard beam. */
            for(int row=0;row<7;++row) {
                int xx=x-6+row*2;
                ht_limb(ht_scene,xx,y-3,xx,y-1,0,0,lit?211:164);
            }
            ht_limb(ht_scene,x-3,y-4,x-2,y,0,0,lit?105:162);
            ht_limb(ht_scene,x-11,y+3,x-6,y+2,1,0,lit?154:202); /* worn hand hollow */
            /* The first fresh scrape grows only behind an actually moved leg. */
            int first=ht_clamp(g->traversal.mill_scrape_left,1119,1209);
            int last=ht_clamp(g->traversal.mill_scrape_right,1119,1209);
            if(last>first)for(int wx=first;wx<last;++wx) {
                int sy=ht_surface_at(g,3,wx)-cy;
                ht_rect(ht_scene,wx-cx,sy,1,1,wx>=1161 && wx<=1206?48:94);
            }
            ht_checkpoint();
        } else {
        ht_rect(ht_scene,x-12,y,24,26,255);ht_rect(ht_scene,x-10,y+2,19,22,150);
        for(int dy=7;dy<24;dy+=6) ht_rect(ht_scene,x-10,y+dy,19,1,235);
        ht_rect(ht_scene,x+7,y+2,3,22,210);
        ht_limb(ht_scene,x-9,y+22,x+6,y+3,2,2,255);
        ht_limb(ht_scene,x-7,y+22,x+8,y+3,0,0,75);
        for(int dx=-8;dx<=8;dx+=16) {ht_rect(ht_scene,x+dx,y+3,1,1,0);ht_rect(ht_scene,x+dx,y+22,1,1,0);}
        }
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
    /* The solved quarry draws its one moving cage with the mechanism;
     * keeping this generic doorway would leave a second stationary frame. */
    if(g->level!=5 || !g->puzzle.solved || g->puzzle.opening!=48){
        ht_rect(ht_scene,x-20,y-52,6,52,255);ht_rect(ht_scene,x+14,y-52,6,52,255);
        ht_rect(ht_scene,x-22,y-56,44,6,255);
    }
}
static void ht_traversal_prompt(const ht_game *g) {
    if(g->level==0) {
        if(ht_traversal_near(g)==HT_SHUTTER) {
            ht_rect(ht_scene,66,33,348,19,255);
            ht_text_color(74,38,g->traversal.mode==HT_SHUTTER?"L/R THROUGH SHUTTER - REVERSE TO GO BACK":"FACE SHUTTER - A TAKE HOLD - L/R CROSS",1,85);
        } else if(ht_traversal_near(g)==HT_CRATE && !ht_mill_register_lit(g)) {
            ht_rect(ht_scene,66,33,348,19,255);
            ht_text_color(74,38,g->traversal.mode==HT_CRATE?"L/R DRAW THE DESK TOWARD THE WINDOW LIGHT":"A GRIP DESK - DRAW IT TOWARD THE LIGHT",1,85);
        }
        return; /* Other forest objects teach through physical consequences. */
    }
    const char *text=NULL;
    switch(ht_traversal_near(g)) {
    case HT_TRESTLE: text=g->traversal.mode==HT_TRESTLE?
        (g->traversal.trestle_phase==HT_TRESTLE_TOUCH && !g->traversal.trestle_tested?
         "A TEST NEXT FOOTING - LEFT TO GO BACK":"L/R FEEL ALONG BRACING - REVERSE TO RETURN"):
         "A TAKE BRACE - L/R LOWER AND CROSS";break;
    case HT_WINDOW: text=g->traversal.mode==HT_WINDOW?"L/R OPEN AND CROSS - REVERSE TO GO BACK":"A TAKE WINDOW - L/R OPEN AND CLIMB";break;
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
