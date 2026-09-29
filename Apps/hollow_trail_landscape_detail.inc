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
/* The marsh ferry begins in a sheltered rock grotto: massive roof at left,
 * eroded headland, bright open water at right. The roof and shore both share
 * their authored silhouette with collision geometry. */
static void ht_boat_grotto(const ht_game *g) {
    if(g->level!=4) return;
    int cx=g->camera/256,cy=g->camera_y/256;
    if(cx>800) return;
    /* Shade only the roof and its narrow fog fringe in row order. A smooth
     * distance ramp avoids stacked polygon bands and repeated opaque fills.
     * Reuse the terrain column scratch; no extra framebuffer or blur pass. */
    uint8_t haze[65];int fade=ht_max(1,64*ht_world_scale/256),last=0;
    for(int d=0;d<=fade;++d) haze[d]=(uint8_t)(255*(fade-d)*(fade-d)/(fade*fade));
    for(int sx=0;sx<HT_W;++sx) {
        int wx=cx+240+(sx-240)*256/ht_world_scale;
        int top=ht_project_y(ht_grotto_ceiling(wx)-cy);
        ht_land_tops[sx]=(int16_t)top;last=ht_max(last,top+fade);
    }
    for(int sy=0;sy<ht_min(HT_H,last);++sy) {
        uint8_t *row=ht_scene+sy*HT_W;
        for(int sx=0;sx<HT_W;++sx) {
            int distance=sy-ht_land_tops[sx];
            if(distance<=0) row[sx]=255;
            else if(distance<fade && row[sx]<haze[distance]) row[sx]=haze[distance];
        }
        if((sy&31)==31) ht_checkpoint();
    }
    /* Thin mineral seams break up the rock without turning it into a tile. */
    ht_limb(ht_scene,-40-cx,18-cy,100-cx,-4-cy,0,0,229);
    ht_limb(ht_scene,100-cx,-4-cy,197-cx,-41-cy,0,0,235);
}
