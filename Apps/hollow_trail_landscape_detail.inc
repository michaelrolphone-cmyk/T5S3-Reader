/* Secondary ground masses sit behind the walking silhouette, like the rock
 * piles and fern beds in the references. Culling is in world space, before
 * projection. Tall details avoid evidence and functional machinery contacts. */
static void ht_landscape_detail(const ht_game *g) {
    const ht_platform *land=ht_level_land(g->level);
    int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
    for(int i=0;i<HT_PLATFORMS;++i) {
        int kind=ht_terrain_kind(g->level,i);
        if(kind==HT_DECK || kind==HT_ROOF || kind==HT_MASONRY || kind==HT_TOWER) continue;
        const ht_platform *p=&land[i];
        int first=ht_floor_div(ht_max(p->left+28,cx-margin-60),83);
        int last=ht_floor_div(ht_min(p->right-28,cx+HT_W+margin+60),83);
        for(int cell=first;cell<=last;++cell) {
            unsigned h=ht_hash((unsigned)cell+g->level*683u);
            int world=cell*83+(int)(h%29),x=world-cx;
            if(world<p->left+24 || world>p->right-24) continue;
            if(g->level==4 && world<700) continue; /* bare grotto shore */
            int base=ht_surface_at(g,i,world)-cy+8;
            int height=12+(int)((h>>8)%20),width=18+(int)((h>>16)%28);
            if(h%3==0 || kind==HT_ROCK || kind==HT_SNOW || kind==HT_SAND) {
                /* Asymmetric angular boulders half buried in the far bank. */
                ht_triangle(ht_scene,x-width,base,x-width/2,base-height,x+width/3,base-height+3,195);
                ht_triangle(ht_scene,x-width,base,x+width/3,base-height+3,x+width,base,195);
                ht_triangle(ht_scene,x+width/3,base-height+3,x+width+3,base-height/3,x+width,base,220);
                ht_limb(ht_scene,x-width/2,base-height,x-width/4,base-height+8,0,0,120);
                if(kind==HT_SNOW) ht_limb(ht_scene,x-width/2,base-height,x+width/3,base-height+3,1,0,100);
            } else if(kind==HT_EARTH || kind==HT_MUD || kind==HT_GARDEN) {
                int stem=base-height;
                ht_limb(ht_scene,x,base,x-4,stem,1,1,185);
                for(int k=0;k<3;++k) {
                    int side=k%2?1:-1,lx=x+side*(9+k*4),ly=stem+k*5;
                    ht_limb(ht_scene,x-2,stem+8,lx,ly,0,0,190);
                    ht_leaf_cluster(ht_scene,lx,ly,9,h+(unsigned)k*31u,185+k*10);
                }
            }
        }
    }
}
/* Three overlapping cave masses enclose a luminous opening. Far and middle
 * walls have slower camera motion; the solid near roof remains the collision
 * curve. The water is a continuous plane behind the entire eroded shore. */
static void ht_boat_grotto(const ht_game *g) {
    if(g->level!=4 || g->camera/256>900) return;
    int cx=g->camera/256,cy=g->camera_y/256;
    static int16_t near_roof[HT_W];
    for(int sx=0;sx<HT_W;++sx) {
        int source=240+(sx-240)*256/ht_world_scale;
        near_roof[sx]=ht_project_y(ht_grotto_ceiling(source+cx)-cy);
        ht_land_tops[sx]=(int16_t)ht_project_y(ht_grotto_ceiling(source+cx*53/100-35)+48-cy*53/100);
        ht_land_bottoms[sx]=(int16_t)ht_project_y(ht_grotto_ceiling(source+cx*22/100-100)+67-cy*22/100);
    }
    int water=ht_project_y(ht_mech(g)->boat_deck+10-cy);
    int glow=ht_project_x(535-cx*22/100),middle=ht_project_y(192-cy*22/100);
    int blend=ht_clamp((900-cx)*256/200,0,256);
    int softness=ht_max(1,22*ht_world_scale/256);
    uint16_t edge[24];
    for(int d=0;d<=softness;++d) edge[d]=(uint16_t)(d*256/softness);
    int boat=ht_project_x(g->traversal.boat_x/256-cx);
    for(int sy=0;sy<HT_H;++sy) {
        uint8_t *row=ht_scene+sy*HT_W;
        for(int sx=0;sx<HT_W;++sx) {
            int dx=sx-glow,dy=sy-middle;
            int ink=ht_clamp(25+(dx*dx+dy*dy*2)/1800,25,112);
            int far=edge[ht_clamp(ht_land_bottoms[sx]-sy+softness,0,softness)];
            int mid=edge[ht_clamp(ht_land_tops[sx]-sy+softness,0,softness)];
            ink=ht_max(ink,ink+(108-ink)*far/256);
            ink=ht_max(ink,ink+(182-ink)*mid/256);
            if(sy>=water) {
                int depth=sy-water;
                /* Broad reflected cave shadows melt into horizontal mist. */
                int water_ink=ht_clamp(42+depth*2/3+ht_abs(dx)/7,42,150);
                int reflection=ht_clamp((235-sx)*2,0,95);
                water_ink=ht_min(205,water_ink+reflection*(80-ht_min(depth,80))/80);
                int shade=ht_max(0,56-ht_abs(sx-boat))*2*ht_max(0,28-depth)/28;
                water_ink=ht_min(210,water_ink+shade);
                int mix=ht_min(256,depth*256/24);
                ink=(ink*(256-mix)+water_ink*mix)/256;
            }
            int distance=sy-near_roof[sx];
            if(distance<=0) ink=255;
            else if(distance<softness) ink=ht_max(ink,255*edge[softness-distance]/256);
            row[sx]=(uint8_t)((ink*blend+row[sx]*(256-blend))/256);
        }
        if((sy&15)==15) ht_checkpoint();
    }
}
