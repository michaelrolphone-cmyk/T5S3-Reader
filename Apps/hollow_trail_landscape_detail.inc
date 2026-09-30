/* Secondary ground masses sit behind the walking silhouette, like the rock
 * piles and fern beds in the references. Culling is in world space, before
 * projection. Tall details avoid evidence and functional machinery contacts. */
static void ht_landscape_detail(const ht_game *g) {
    if(g->level==3)return; /* Track ballast is drawn at rail scale, not as boulders. */
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
            if((g->level==2 || g->level==5 || g->level==8) && (cell&1))continue;
            int world=cell*83+(int)(h%29),x=world-cx;
            if(world<p->left+24 || world>p->right-24) continue;
            if(g->level==4 && world<700) continue; /* bare grotto shore */
            int base=ht_surface_at(g,i,world)-cy+8;
            int height=12+(int)((h>>8)%20),width=18+(int)((h>>16)%28);
            if(h%3==0 || kind==HT_ROCK || kind==HT_SNOW || kind==HT_SAND) {
                /* Asymmetric angular boulders half buried in the far bank. */
                if(g->level==0) {
                    /* Weathered shoulders, shallow chips and a buried toe.
                     * The long planes survive at distance; chips resolve close. */
                    static const int8_t edge[28][2]={
                        {-100,0},{-99,-12},{-95,-21},{-94,-34},{-86,-41},{-83,-52},
                        {-71,-62},{-67,-73},{-55,-81},{-47,-88},{-32,-90},{-23,-97},
                        {-10,-100},{2,-99},{13,-94},{26,-92},{34,-84},{45,-82},
                        {54,-71},{65,-66},{70,-55},{78,-49},{83,-37},{91,-31},
                        {94,-19},{99,-11},{100,0},{0,7}
                    };
                    int flip=(h&64u)?-1:1;
                    for(int v=0;v<28;++v) {
                        int n=(v+1)%28;
                        ht_triangle(ht_scene,x,base-height/3,
                            x+flip*edge[v][0]*width/100,base+edge[v][1]*height/100,
                            x+flip*edge[n][0]*width/100,base+edge[n][1]*height/100,
                            v>=16 && v<=24?207:195);
                    }
                    ht_limb(ht_scene,x-width/4,base-height*4/5,x-width/8,base-height/2,0,0,154);
                    ht_limb(ht_scene,x-width/8,base-height/2,x+width/7,base-height*2/5,0,0,158);
                } else if(g->level!=4 && g->level!=1) {
                    /* Twelve-point weathered outline on the existing rocks;
                     * long shoulders, rounded corners, a partly buried base. */
                    static const int8_t outline[12][2]={
                        {-100,0},{-94,-31},{-77,-68},{-52,-93},
                        {-22,-100},{8,-96},{34,-85},{57,-70},
                        {79,-47},{96,-21},{100,0},{0,7}
                    };
                    for(int v=0;v<12;++v) {
                        int next=(v+1)%12;
                        ht_triangle(ht_scene,x,base-height/3,
                            x+outline[v][0]*width/100,base+outline[v][1]*height/100,
                            x+outline[next][0]*width/100,base+outline[next][1]*height/100,
                            v>=6 && v<=9?205:195);
                    }
                } else {
                    ht_triangle(ht_scene,x-width,base,x-width/2,base-height,x+width/3,base-height+3,195);
                    ht_triangle(ht_scene,x-width,base,x+width/3,base-height+3,x+width,base,195);
                    ht_triangle(ht_scene,x+width/3,base-height+3,x+width+3,base-height/3,x+width,base,220);
                }
                if(g->level!=0)ht_limb(ht_scene,x-width/2,base-height,x-width/4,base-height+8,0,0,120);
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
static __attribute__((optimize("O3"))) void ht_boat_grotto(const ht_game *g) {
    if(g->level!=4 || g->camera/256>900) return;
    int cx=g->camera/256,cy=g->camera_y/256;
    int blend=ht_clamp((900-cx)*256/200,0,256);
    if(!blend) return;
    static int16_t near_roof[HT_W];
    for(int sx=0;sx<HT_W;++sx) {
        int source=240+(sx-240)*256/ht_world_scale;
        near_roof[sx]=ht_project_y(ht_grotto_ceiling(source+cx)-cy);
        ht_land_tops[sx]=(int16_t)ht_project_y(ht_cave_face(source+cx*53/100,0)-cy*53/100);
        ht_land_bottoms[sx]=(int16_t)ht_project_y(ht_cave_face(source+cx*22/100,1)-cy*22/100);
    }
    int water=ht_project_y(ht_mech(g)->boat_deck+10-cy);
    int glow=ht_project_x(535-cx*22/100),middle=ht_project_y(192-cy*22/100);
    int softness=ht_max(1,8*ht_world_scale/256);
    uint16_t edge[24],wall_edge[24];
    for(int d=0;d<=softness;++d) {
        edge[d]=(uint16_t)(d*256/softness);
        wall_edge[d]=edge[d]/3;
    }
    int boat=ht_project_x(g->traversal.boat_x/256-cx);
    for(int sy=0;sy<HT_H;++sy) {
        uint8_t *row=ht_scene+sy*HT_W;
        int dy=sy-middle,dy2=dy*dy*2;
        int far_ink=145-ht_clamp(sy-50,0,180)/2;
        int mid_ink=222-ht_clamp(sy-80,0,160)/3;
        int depth=sy-water;
        int water_ink=110+depth/3,reflection_depth=80-ht_min(depth,80);
        int shade_depth=ht_max(0,34-depth);
        bool broken_shade=(depth/3)%3==1;
        bool ripple_row=depth>4 && depth%11==0;
        for(int sx=0;sx<HT_W;++sx) {
            int distance=sy-near_roof[sx],ink;
            /* Opaque rock and water replace the lighting underneath. Reject
             * that work before evaluating glow, far walls or middle walls. */
            if(distance<=0) ink=255;
            else {
                int dx=sx-glow;
                if(depth>=0) {
                    ink=ht_clamp(water_ink+ht_abs(dx)/12,110,170);
                    int reflection=ht_clamp((235-sx)*2,0,70);
                    ink=ht_min(210,ink+reflection*reflection_depth/80);
                    int spread=ht_max(0,48-ht_abs(sx-boat));
                    int shade=spread*shade_depth/17;
                    if(broken_shade) shade=shade*2/3;
                    ink=ht_min(225,ink+shade);
                    if(depth<=1) ink=ht_max(65,ink-32);
                    if(ripple_row && (sx+cx/2+depth*7)%83<17) ink=ht_max(70,ink-9);
                } else {
                    ink=ht_clamp(18+(dx*dx+dy2)/1300,18,125);
                    int far_distance=sy-ht_land_bottoms[sx];
                    if(far_distance<=0) ink=ht_max(ink,far_ink);
                    else if(far_distance<softness) {
                        int far=wall_edge[softness-far_distance];
                        ink=ht_max(ink,ink+(far_ink-ink)*far/256);
                    }
                    int mid_distance=sy-ht_land_tops[sx];
                    if(mid_distance<=0) ink=ht_max(ink,mid_ink);
                    else if(mid_distance<softness) {
                        int mid=wall_edge[softness-mid_distance];
                        ink=ht_max(ink,ink+(mid_ink-ink)*mid/256);
                    }
                }
                if(distance<softness) ink=ht_max(ink,150*edge[softness-distance]/256);
            }
            /* Full opacity neither reads nor blends the discarded scene. */
            row[sx]=(uint8_t)(blend==256?ink:(ink*blend+row[sx]*(256-blend))/256);
        }
        if((sy&15)==15) ht_checkpoint();
    }
}
