/* World-anchored detail stays still when the camera breathes. Whole visible
 * columns share the exact collision profile; coalesce equal spans for raster
 * locality instead of introducing another full-frame texture/filter pass. */
static int16_t ht_land_tops[HT_W+256],ht_land_bottoms[HT_W+256];
static void ht_draw_land(const ht_game *g,int i,int part,int l,int r,int flat) {
    int cx=g->camera/256,cy=g->camera_y/256,kind=ht_terrain_kind(g->level,i);
    int begin=ht_max(l,cx-ht_view_margin()),end=ht_min(r,cx+HT_W+ht_view_margin());
    if(begin>=end) return;
    int common_top=-520,common_bottom=520;
    for(int at=begin;at<end;++at) {
        int top=part==2?flat:ht_surface_at(g,i,at),bottom=ht_terrain_bottom(g,i,at);
        ht_land_tops[at-begin]=(int16_t)top;ht_land_bottoms[at-begin]=(int16_t)bottom;
        common_top=ht_max(common_top,top);common_bottom=ht_min(common_bottom,bottom);
    }
    /* Fill the shared solid body with contiguous row writes, then only the
     * shallow curved fringe and any recessed wall. No per-column deep fill. */
    if(common_bottom>common_top) ht_rect(ht_scene,begin-cx,common_top-cy,end-begin,common_bottom-common_top,255);
    for(int at=begin;at<end;) {
        int top=ht_land_tops[at-begin],bottom=ht_land_bottoms[at-begin],next=at+1;
        while(next<end && ht_land_tops[next-begin]==top && ht_land_bottoms[next-begin]==bottom) ++next;
        ht_rect(ht_scene,at-cx,top-cy,next-at,ht_min(common_top,bottom)-top,255);
        if(bottom>common_bottom) ht_rect(ht_scene,at-cx,ht_max(top,common_bottom)-cy,next-at,bottom-ht_max(top,common_bottom),255);
        at=next;
    }
    if(kind==HT_DECK) {
        int y=flat-cy,x=l-cx,w=r-l;
        ht_rect(ht_scene,x,y+4,w,2,185);
        if(g->level==3) ht_rect(ht_scene,x,y-2,w,2,220);
        for(int at=ht_max(l+6,cx-20);at<end;at+=21) {
            ht_rect(ht_scene,at-cx,y+2,1,12,130);
            ht_limb(ht_scene,at-cx,y+14,at+18-cx,y+6,1,1,205);
        }
        if(i!=ht_mechanics_by_level[g->level].bridge) {
            /* Gantries are supported, visibly tied to the industrial site. */
            for(int side=0;side<2;++side) {
                int foot=side?r-18:l+18;
                ht_rect(ht_scene,foot-cx,flat+18-cy,6,520-flat,230);
                ht_limb(ht_scene,foot-cx,flat+66-cy,foot+(side?-38:38)-cx,flat+18-cy,2,2,220);
            }
        }
        return;
    }
    /* Unequal tufts and silt/ballast, never the same decorated shelf repeated. */
    for(int at=ht_floor_div(begin-8,9)*9;at<end;at+=9) {
        if(at<l+3 || at>=r-3) continue;
        unsigned h=ht_hash((unsigned)at+g->level*137u);
        int top=part==2?flat:ht_surface_at(g,i,at),x=at-cx,y=top-cy;
        if(kind==HT_EARTH || kind==HT_MUD || kind==HT_GARDEN) {
            int height=2+(int)(h%7);
            ht_limb(ht_scene,x,y+1,x+(int)(h%7)-3,y-height,0,0,235);
            if((h&3u)==0) ht_limb(ht_scene,x+2,y+1,x+5,y-height/2,0,0,255);
            if(kind==HT_MUD && h%5==0) {
                ht_limb(ht_scene,x,y,x-3,y-19,0,0,225);
                ht_limb(ht_scene,x-3,y-19,x-3,y-24,1,1,205);
            }
        } else if(kind==HT_BALLAST) {
            ht_rect(ht_scene,x,y-2,5,2,210);
            ht_rect(ht_scene,x,y+2,7,1,180);
        } else if(kind==HT_SNOW) ht_limb(ht_scene,x,y+1,x+7,y+2,1,0,135);
        if((h&3u)==1 && top+18<ht_terrain_bottom(g,i,at)) {
            if(kind==HT_EARTH || kind==HT_MUD) {
                ht_limb(ht_scene,x,y+5,x+5,y+15,0,0,195);
                ht_limb(ht_scene,x+5,y+15,x+3,y+22,0,0,215);
            } else if(kind==HT_ROCK || kind==HT_SNOW || kind==HT_SAND) {
                ht_limb(ht_scene,x,y+12,x+12,y+20,0,0,215);
                ht_limb(ht_scene,x+12,y+20,x+8,y+37,0,0,225);
            }
        }
    }
    if(kind==HT_ROOF || kind==HT_MASONRY || kind==HT_TOWER || kind==HT_GARDEN) {
        /* Buildings have walls; conservatory beds have retained brick earth. */
        for(int at=ht_floor_div(begin,61)*61;at<end;at+=61) {
            int top=ht_surface_at(g,i,at),bottom=ht_terrain_bottom(g,i,at);
            if(at<l+12 || at+23>=r || bottom<top+60) continue;
            int x=at-cx,y=top+23-cy;
            if(kind==HT_ROOF || kind==HT_TOWER) {
                ht_rect(ht_scene,x,y,19,33,200);
                ht_rect(ht_scene,x+3,y+3,13,25,232);
                ht_rect(ht_scene,x+8,y+3,2,25,255);
            } else {
                ht_limb(ht_scene,x,y,x+35,y+1,0,0,210);
                ht_limb(ht_scene,x+16,y+1,x+16,y+13,0,0,210);
            }
        }
    }
}
