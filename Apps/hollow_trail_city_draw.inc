/* Authored roofscape: three whole building planes, with streets disappearing
 * below the haze. Geometry is fixed in world space and projected once. */
typedef struct {int16_t x,width,top;uint8_t crown;} ht_city_mass;
static void ht_city_mass_draw(const ht_city_mass *b,int cx,int cy,int plane) {
    static const int rates[3]={16,34,58},tones[3]={75,124,180};
    int rate=rates[plane],x=b->x-cx*rate/100;
    int perspective=plane==0?45:plane==1?70:100;
    int y=240-(240-b->top)*perspective/100-cy*rate/100;
    int w=b->width*(plane==0?85:plane==1?82:100)/100;
    int margin=ht_view_margin(),tone=tones[plane];
    if(x+w+12< -margin || x-12>HT_W+margin)return;
    int bottom=560-cy*rate/100;
    ht_rect(ht_scene,x,y,w,bottom-y,tone);
    /* A broad light-facing side, not an outline glow around every object. */
    ht_rect(ht_scene,x+w*3/4,y,w/4,bottom-y,tone-26);
    if(b->crown==1) {
        ht_rect(ht_scene,x+w/6,y-22,w*2/3,24,tone);
        ht_rect(ht_scene,x+w/3,y-41,w/3,20,tone);
        ht_limb(ht_scene,x+w/2,y-40,x+w/2,y-78,1,0,tone);
    } else if(b->crown==2) {
        ht_triangle(ht_scene,x-4,y,x+w/2,y-31,x+w+4,y,tone);
    } else ht_rect(ht_scene,x-3,y,w+6,4,tone+8);
    /* Windows only on the two nearer planes, with both row/column culling.
     * A small number of broad bays survives the panel's monochrome sampling. */
    if(plane) {
        int low=135+(0-135)*256/ht_world_scale;
        int high=135+(HT_H-135)*256/ht_world_scale;
        int start=ht_max(0,ht_floor_div(low-y-16,38));
        int end=ht_min(19,(high-y)/38+1);
        for(int row=start;row<end;++row) {
            int wy=y+16+row*38;
            for(int col=0;col<4;++col) {
                int wx=x+12+col*(w-24)/4;
                if(wx< -margin-20 || wx>HT_W+margin)continue;
                int ink=tone+10;
                if(plane==2 && (row+col+(b->x/10))%29==0)ink=65;
                ht_rect(ht_scene,wx,wy,ht_max(3,(w-24)/4-16),19,ink);
                if(plane==2)ht_rect(ht_scene,wx+2,wy+19,ht_max(2,(w-24)/4-18),1,tone+9);
            }
        }
    }
    ht_checkpoint();
}
static void ht_city_backdrop_low(const ht_game *g) {
    if(g->level!=1)return;
    int cx=g->camera/256,cy=g->camera_y/256;
    uint8_t *cached=ht_cached_near[2][0];
    if(ht_forest_frame_valid && ht_composed_frame_level==1 && ht_forest_frame_x==cx &&
       ht_forest_frame_y==cy && ht_forest_frame_scale==ht_world_scale) {
        memcpy(ht_scene,cached,HT_PIXELS);return;
    }
    ht_forest_frame_valid=false;
    int light=ht_project_x(390-cx*16/100),height=ht_project_y(-110-cy*16/100);
    /* Pale opening over the city canyon. Two-pixel sky samples are expanded
     * in place; no full-frame blur or extra allocation. */
    for(int y=0;y<HT_H;y+=2) {
        int dy=y-height;
        uint8_t *row=ht_scene+y*HT_W;
        for(int x=0;x<HT_W;x+=2) {
            int dx=x-light;
            row[x]=row[x+1]=(uint8_t)ht_clamp(16+(dx*dx+dy*dy/3)/6200,16,70);
        }
        memcpy(row+HT_W,row,HT_W);
        if((y&15)==14)ht_checkpoint();
    }
    static const ht_city_mass city[3][9]={
        {{-230,145,24,0},{-50,120,-52,1},{103,88,45,0},{225,162,-102,1},
         {431,116,-18,2},{580,165,-145,1},{795,116,16,0},{951,157,-72,0},{1140,180,-121,1}},
        {{-260,160,-85,2},{-28,147,53,0},{200,136,-163,1},{413,190,-40,0},
         {680,151,-225,1},{915,195,-94,2},{1170,125,-183,0},{1370,201,-72,1},{1650,174,-200,0}},
        {{-330,185,-28,0},{-32,160,170,2},{330,158,105,1},{654,182,120,0},
         {967,170,70,2},{1355,196,-100,1},{1740,165,-165,0},{2070,213,27,2},{2430,180,-70,1}}
    };
    for(int plane=0;plane<3;++plane) {
        for(unsigned n=0;n<9;++n)ht_city_mass_draw(&city[plane][n],cx,cy,plane);
        /* Low mist softens foundations while roof edges remain crisp. Draw
         * only visible rows, with one shared blend weight per row. */
        int horizon=ht_project_y(225-cy*(16+plane*21)/100);
        for(int y=ht_max(0,horizon);y<HT_H;++y) {
            int weight=ht_min(120,(y-horizon)*2),fog=49+plane*18;
            uint8_t *row=ht_scene+y*HT_W;
            for(int x=0;x<HT_W;++x)row[x]=(uint8_t)((row[x]*(256-weight)+fog*weight)/256);
            if((y&15)==15)ht_checkpoint();
        }
    }
    if(!ht_abort) {
        memcpy(cached,ht_scene,HT_PIXELS);
        ht_composed_frame_level=1;ht_forest_frame_valid=true;
        ht_forest_frame_x=cx;ht_forest_frame_y=cy;ht_forest_frame_scale=ht_world_scale;
    }
}
static void ht_city_backdrop(const ht_game *g) {
    if(g->level!=1)return;
    if(!ht_native_active){ht_city_backdrop_low(g);return;}
    uint8_t *native=ht_scene;
    ht_scene=ht_scene_low;ht_native_active=false;
    ht_city_backdrop_low(g);
    ht_scene=native;ht_native_active=true;
    ht_expand_scene_native(ht_scene_low,native);
}
/* Roof furniture sits beyond the walkable front edge. Fire escapes are
 * attached to the actual ladder recesses; none masquerades as a jump target. */
static void ht_city_roof_detail(const ht_game *g) {
    if(g->level!=1)return;
    int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
    const ht_platform *land=ht_level_land(1);
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&land[i];
        if(p->right<cx-margin || p->left>cx+HT_W+margin)continue;
        int x=p->left-cx,y=p->top-cy;
        /* Continuous coping and a shallow front lip anchor roof to facade. */
        ht_rect(ht_scene,x,y+2,p->right-p->left,3,155);
        ht_rect(ht_scene,x+4,y+7,p->right-p->left-8,2,223);
        if(i==0 || i==3 || i==8) {
            int at=p->left+90-cx;
            ht_rect(ht_scene,at,y-16,42,16,224);
            ht_rect(ht_scene,at-3,y-18,48,3,250);
            for(int vent=0;vent<4;++vent)ht_rect(ht_scene,at+5+vent*8,y-12,3,9,170);
        }
        /* Drainpipes and long facade bays disappear below the camera. */
        int pipe=p->right-17-cx;
        ht_limb(ht_scene,pipe,y+8,pipe,y+210,1,1,180);
        for(int joint=0;joint<4;++joint)ht_rect(ht_scene,pipe-3,y+24+joint*48,7,2,213);
    }
    const ht_mechanics *m=ht_mech(g);
    for(int i=0;i<3;++i) {
        int wx=m->ladders[i].x;if(!wx)continue;
        int x=wx-cx,top,bottom;ht_ladder_ends(1,i,&top,&bottom);
        if(x< -80-margin || x>HT_W+80+margin)continue;
        /* One shielded lamp washes the service doorway and adjacent wall. */
        ht_triangle(ht_scene,x+34,bottom-59-cy,x+14,bottom-cy,x+55,bottom-cy,202);
        ht_rect(ht_scene,x+29,bottom-59-cy,10,3,90);
        ht_rect(ht_scene,x+27,bottom-62-cy,14,3,250);
        /* Closed service door alongside the ladder, with a projecting lintel. */
        ht_rect(ht_scene,x+20,bottom-50-cy,28,50,218);
        ht_rect(ht_scene,x+24,bottom-46-cy,20,46,244);
        ht_rect(ht_scene,x+18,bottom-53-cy,32,3,180);
        ht_rect(ht_scene,x+38,bottom-24-cy,3,2,130);
        for(int y=top+28;y<bottom;y+=40) {
            ht_limb(ht_scene,x-9,y-cy,x-21,y+10-cy,1,1,200);
            ht_limb(ht_scene,x+9,y-cy,x+19,y+10-cy,1,1,200);
        }
    }
}
/* Two near-camera masonry corners pass the observer, leaving the central
 * traversal view open for most of the chapter. Stepped cornices and chipped
 * lower edges carry contour detail appropriate to architecture. */
static void ht_city_observer_foreground(const ht_game *g) {
    if(g->level!=1)return;
    static const int anchors[2]={1240,3340};
    int cx=g->camera/256*5/4,cy=g->camera_y/256*5/4;
    static const ht_foreground_section pier[]={
        {-29,-650,29,-650},{-29,-210,29,-210},{-34,-202,34,-202},
        {-34,-196,34,-196},{-27,-190,27,-190},{-26,-60,26,-60},
        {-28,24,25,24},{-25,32,29,32},{-29,43,27,43},{-29,520,29,520}
    };
    for(int n=0;n<2;++n) {
        int x=anchors[n]-cx;
        if(x< -40-ht_view_margin() || x>HT_W+40+ht_view_margin())continue;
        ht_foreground_contour(x,-cy,pier,sizeof(pier)/sizeof(pier[0]),249);
        ht_limb(ht_scene,x+20,-600-cy,x+20,520-cy,1,1,214);
    }
}
