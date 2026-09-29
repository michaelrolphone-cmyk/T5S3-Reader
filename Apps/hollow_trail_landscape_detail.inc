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
/* Independently authored cave faces: shelves and near-vertical fractures,
 * rather than scaled copies of the foreground roof. */
static int ht_cave_face(int x,int layer) {
    static const int16_t faces[2][14][2]={
        {{-300,262},{100,245},{210,242},{250,229},{280,223},{306,187},{320,183},{336,158},{368,153},{398,125},{415,117},{436,90},{454,-60},{480,-280}},
        {{-300,286},{130,279},{260,269},{330,263},{378,249},{406,224},{424,215},{438,188},{470,181},{486,128},{503,120},{520,89},{537,-100},{550,-280}}
    };
    for(int i=1;i<14;++i) if(x<=faces[layer][i][0]) {
        int a=faces[layer][i-1][0],b=faces[layer][i][0];
        int y=faces[layer][i-1][1],end=faces[layer][i][1];
        return y+(end-y)*ht_clamp(x-a,0,b-a)/(b-a);
    }
    return -520;
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
        ht_land_tops[sx]=(int16_t)ht_project_y(ht_cave_face(source+cx*53/100,0)-cy*53/100);
        ht_land_bottoms[sx]=(int16_t)ht_project_y(ht_cave_face(source+cx*22/100,1)-cy*22/100);
    }
    int water=ht_project_y(ht_mech(g)->boat_deck+10-cy);
    int glow=ht_project_x(535-cx*22/100),middle=ht_project_y(192-cy*22/100);
    int blend=ht_clamp((900-cx)*256/200,0,256);
    int softness=ht_max(1,8*ht_world_scale/256);
    uint16_t edge[24];
    for(int d=0;d<=softness;++d) edge[d]=(uint16_t)(d*256/softness);
    int boat=ht_project_x(g->traversal.boat_x/256-cx);
    for(int sy=0;sy<HT_H;++sy) {
        uint8_t *row=ht_scene+sy*HT_W;
        for(int sx=0;sx<HT_W;++sx) {
            int dx=sx-glow,dy=sy-middle;
            int ink=ht_clamp(18+(dx*dx+dy*dy*2)/1300,18,125);
            int far_distance=sy-ht_land_bottoms[sx];
            int far=far_distance<=0?256:edge[ht_clamp(softness-far_distance,0,softness)]/3;
            int mid_distance=sy-ht_land_tops[sx];
            int mid=mid_distance<=0?256:edge[ht_clamp(softness-mid_distance,0,softness)]/3;
            int far_ink=145-ht_clamp(sy-50,0,180)/2;
            ink=ht_max(ink,ink+(far_ink-ink)*far/256);
            int mid_ink=222-ht_clamp(sy-80,0,160)/3;
            ink=ht_max(ink,ink+(mid_ink-ink)*mid/256);
            if(sy>=water) {
                int depth=sy-water;
                /* The water is its own horizontal surface, not cave fog.
                 * A dark plane, reflected hull and sparse ripples retain a
                 * visible waterline even beneath the brightest opening. */
                ink=ht_clamp(110+depth/3+ht_abs(dx)/12,110,170);
                int reflection=ht_clamp((235-sx)*2,0,70);
                ink=ht_min(210,ink+reflection*(80-ht_min(depth,80))/80);
                int spread=ht_max(0,48-ht_abs(sx-boat));
                int shade=spread*ht_max(0,34-depth)/17;
                if((depth/3)%3==1) shade=shade*2/3;
                ink=ht_min(225,ink+shade);
                if(depth<=1) ink=ht_max(65,ink-32);
                int ripple=(sx+cx/2+depth*7)%83;
                if(depth>4 && depth%11==0 && ripple<17) ink=ht_max(70,ink-9);
            }
            int distance=sy-near_roof[sx];
            if(distance<=0) ink=255;
            else if(distance<softness) ink=ht_max(ink,150*edge[softness-distance]/256);
            row[sx]=(uint8_t)((ink*blend+row[sx]*(256-blend))/256);
        }
        if((sy&15)==15) ht_checkpoint();
    }
}
