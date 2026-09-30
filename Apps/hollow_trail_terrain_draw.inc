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
    bool cliff=part==0 && common_bottom==520 &&
        ((ht_cliff_edge(g,i,-1) && l>=begin && l<end) ||
         (ht_cliff_edge(g,i,1) && r>begin && r<=end));
    if(cliff) {
        int first=common_top;
        for(int at=begin;at<end;++at) first=ht_min(first,ht_land_tops[at-begin]);
        int margin=(HT_H*256/ht_world_scale-HT_H)/2+2;
        bool left=ht_cliff_edge(g,i,-1),right=ht_cliff_edge(g,i,1);
        for(int y=ht_max(first,cy-margin);y<ht_min(520,cy+HT_H+margin);++y) {
            int low=ht_max(begin,l+(left?ht_cliff_inset(g,i,-1,y):0));
            int high=ht_min(end,r-(right?ht_cliff_inset(g,i,1,y):0));
            if(y>=common_top) ht_rect(ht_scene,low-cx,y-cy,high-low,1,255);
            else for(int at=low;at<high;) {
                while(at<high && y<ht_land_tops[at-begin]) ++at;
                int start=at;
                while(at<high && y>=ht_land_tops[at-begin]) ++at;
                ht_rect(ht_scene,start-cx,y-cy,at-start,1,255);
            }
        }
    } else {
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
    for(int at=ht_floor_div(begin-8,5)*5;at<end;at+=5) {
        if(at<l+3 || at>=r-3) continue;
        unsigned h=ht_hash((unsigned)at+g->level*137u);
        int top=part==2?flat:ht_surface_at(g,i,at),x=at-cx,y=top-cy;
        if(kind==HT_EARTH || kind==HT_MUD || kind==HT_GARDEN) {
            unsigned patch=ht_hash((unsigned)ht_floor_div(at,67)+g->level*83u);
            int height=2+(int)(h%5)+(int)(patch%7);
            if(g->level==4 && at<700) height=1+(int)(h%3);
            for(int blade=0;blade<3;++blade) {
                int root=x+blade-1,lean=(int)((h>>(blade*4))%9)-4;
                ht_limb(ht_scene,root,y+2,root+lean,y-height+blade*2,0,0,235+blade*10);
            }
            if((h&15u)==0 && !(g->level==4 && at<700)) ht_fern(ht_scene,x,y+3,12+(int)(patch%13),240,h);
            if(kind==HT_MUD && h%5==0 && at>=700) {
                ht_limb(ht_scene,x,y,x-3,y-19,0,0,225);
                ht_limb(ht_scene,x-3,y-19,x-3,y-24,1,1,205);
            }
        } else if(kind==HT_BALLAST) {
            ht_rect(ht_scene,x,y-2,5,2,210);
            ht_rect(ht_scene,x,y+2,7,1,180);
        } else if(kind==HT_SNOW) ht_limb(ht_scene,x,y+1,x+7,y+2,1,0,135);
        if((h&3u)==1 && top+18<ht_terrain_bottom(g,i,at)) {
            if(kind==HT_EARTH || kind==HT_MUD) {
                if((h&31u)==1) {
                    ht_limb(ht_scene,x,y+7,x+7,y+11,0,0,224);
                    ht_limb(ht_scene,x+7,y+11,x+12,y+9,0,0,235);
                }
            } else if(kind==HT_ROCK || kind==HT_SNOW || kind==HT_SAND) {
                if(g->level==4) {
                    ht_limb(ht_scene,x,y+12,x+12,y+20,0,0,215);
                    ht_limb(ht_scene,x+12,y+20,x+8,y+37,0,0,225);
                } else if((h&31u)==1) {
                    /* Sparse faults, rather than a repeated chevron texture. */
                    ht_limb(ht_scene,x,y+17,x+13,y+23,0,0,220);
                    ht_limb(ht_scene,x+13,y+23,x+26,y+21,0,0,235);
                }
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

/* A buried trunk and curved buttress roots, tessellated against the soil.
 * No rectangular pedestals or one-pixel vertical extension strips. */
static void ht_grounded_tree(const ht_game *g,int i,int world,int height,int width,unsigned seed) {
    int cx=g->camera/256,cy=g->camera_y/256;
    int base=ht_surface_at(g,i,world),buried=ht_buried_base(g,i,world,width);
    ht_tree(ht_scene,world-cx,buried-cy,height+buried-base,width,seed,255);
    ht_detail_tree_bark(g,world-cx,buried-cy,height+buried-base,width,seed);
    const ht_platform *p=&ht_level_land(g->level)[i];
    for(int side=-1;side<=1;side+=2) {
        int end=ht_clamp(world+side*width*(side<0?3:2),p->left+2,p->right-3);
        int reach=ht_abs(end-world),previous=world;
        int oldtop=base-height/7,oldsoil=ht_surface_at(g,i,world)+3;
        for(int step=4;step<reach+4;step+=4) {
            int at=ht_min(step,reach),wx=world+side*at;
            int soil=ht_surface_at(g,i,wx)+3;
            int u=(reach-at)*256/ht_max(1,reach);
            int top=ht_min(soil-6,base-(height/7)*u*u/65536);
            if(g->level==0 && at<reach-8)top-=ht_max(0,3-ht_abs(at-reach/2)/3);
            ht_triangle(ht_scene,previous-cx,oldtop-cy,wx-cx,top-cy,previous-cx,oldsoil-cy,255);
            ht_triangle(ht_scene,wx-cx,top-cy,wx-cx,soil-cy,previous-cx,oldsoil-cy,255);
            if(g->level==0 && top+9<soil && oldtop+9<oldsoil && at>width/2)
                ht_limb(ht_scene,previous-cx,oldtop+4-cy,wx-cx,top+4-cy,0,0,220);
            previous=wx;oldtop=top;oldsoil=soil;
        }
    }
}
