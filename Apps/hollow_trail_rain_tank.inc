/* Novella II: shelter after the burnt fuse, then the light across the court.
 * Every footing stays in the authored city route. Dressing is behind the
 * character; the short service ladder ends at a visibly chained tank hatch. */
#define HT_RAIN_TANK_X 1840
static int ht_rain_tank_floor(void) { return ht_level_land(1)[6].top; }
static int ht_rain_tank_weather_floor(const ht_game *g,int x0,int x1,int floor) {
    if(g->level!=1)return floor;
    int cx=g->camera/256;
    /* Check the whole streak, so wind cannot draw an outside drop across the
     * shelter edge. This visual cover adds no collision or character mask. */
    if(ht_max(x0,x1)+cx>HT_RAIN_TANK_X-90 && ht_min(x0,x1)+cx<HT_RAIN_TANK_X+89)
        floor=ht_min(floor,ht_rain_tank_floor()-139-g->camera_y/256);
    return floor;
}
static int ht_rain_window_x(void) { return ht_mechanics_by_level[1].ladders[2].x+38; }
static bool ht_rain_window_lit(unsigned ticks) { return ticks%144u<54u && ticks%18u<9u; }
static void ht_city_rain_tank(const ht_game *g) {
    if(g->level!=1)return;
    int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
    int x=HT_RAIN_TANK_X-cx,floor=ht_rain_tank_floor()-cy;
    if(x+112>=-margin && x-110<=HT_W+margin) {
        /* Two planes of riveted iron, with a curved heavy belly and a raised
         * rim. Quiet surface streaks leave the silhouette readable in mono. */
        ht_rect(ht_scene,x-92,floor-137,181,58,185);
        ht_rect(ht_scene,x+45,floor-136,44,57,220);
        ht_triangle(ht_scene,x-92,floor-80,x+89,floor-80,x+63,floor-60,228);
        ht_triangle(ht_scene,x-92,floor-80,x+63,floor-60,x-67,floor-60,228);
        ht_limb(ht_scene,x-94,floor-138,x+90,floor-138,3,3,239);
        ht_limb(ht_scene,x-86,floor-132,x+80,floor-132,1,1,122);
        for(int band=0;band<3;++band) {
            int yy=floor-125+band*20;
            ht_limb(ht_scene,x-90,yy,x+87,yy,0,0,231);
            for(int rivet=0;rivet<10;++rivet)ht_disk(ht_scene,x-80+rivet*17,yy,1,248);
        }
        for(int seam=0;seam<9;++seam) {
            int xx=x-80+seam*19;
            ht_limb(ht_scene,xx,floor-132,xx+2,floor-97+(seam%3)*7,0,0,145);
            ht_rect(ht_scene,xx+2,floor-90+(seam%3)*3,2,5,210);
        }
        /* Rear bracing rises to the tank, above head height. The front
         * support pair leaves the player's shelter open, not a false wall. */
        ht_limb(ht_scene,x-66,floor-63,x-79,floor,4,5,201);
        ht_limb(ht_scene,x+63,floor-63,x+77,floor,4,5,232);
        ht_limb(ht_scene,x-62,floor-61,x+62,floor-40,1,1,162);
        ht_limb(ht_scene,x+62,floor-61,x-62,floor-40,1,1,162);
        for(int side=-1;side<=1;side+=2) {
            int xx=x+side*78;ht_rect(ht_scene,xx-9,floor-3,18,3,246);
            for(int i=0;i<5;++i)ht_rect(ht_scene,xx-2+(i&1)*3,floor-12-i*9,2,3,133);
        }
        /* Service ladder cannot be mistaken for the tall traversable ladder
         * across the court: it ends at the underside hatch, behind the legs. */
        ht_limb(ht_scene,x+48,floor-16,x+40,floor-62,1,1,181);
        ht_limb(ht_scene,x+60,floor-16,x+52,floor-62,1,1,181);
        for(int i=0;i<6;++i)ht_limb(ht_scene,x+47-i,floor-21-i*7,x+58-i,floor-21-i*7,0,0,207);
        ht_rect(ht_scene,x+32,floor-66,31,5,123);
        ht_limb(ht_scene,x+34,floor-68,x+61,floor-59,1,1,251);
        ht_limb(ht_scene,x+60,floor-68,x+35,floor-59,1,1,251);
        ht_rect(ht_scene,x+45,floor-64,6,7,246);
        /* Drops shed from the belly; puddles reflect the support feet. */
        for(int i=0;i<7;++i) {
            int xx=x-62+i*20,yy=floor-58+(int)((g->ticks*3u+i*13u)%48u);
            ht_limb(ht_scene,xx,yy,xx-1,yy+3,0,0,122);
        }
        ht_limb(ht_scene,x-87,floor+2,x-51,floor+2,0,0,54);
        ht_limb(ht_scene,x+59,floor+2,x+90,floor+2,0,0,54);
        ht_checkpoint();
    }
    /* The window is on the real upper roof reached by ladder 2, matching
     * the schoolroom's third route anchor, not an unrelated distant beacon. */
    int wx=ht_rain_window_x()-cx,roof=ht_level_land(1)[7].top-cy;
    if(wx+69>=-margin && wx-42<=HT_W+margin) {
        ht_rect(ht_scene,wx-38,roof-88,101,88,190);
        ht_rect(ht_scene,wx+44,roof-87,19,87,223);
        ht_triangle(ht_scene,wx-44,roof-88,wx+11,roof-107,wx+69,roof-88,234);
        ht_limb(ht_scene,wx-44,roof-88,wx+69,roof-88,2,2,246);
        for(int row=0;row<7;++row) {
            int yy=roof-8-row*11;
            ht_limb(ht_scene,wx-36,yy,wx+42,yy,0,0,164);
            ht_limb(ht_scene,wx-20+(row%3)*21,yy,wx-20+(row%3)*21,yy-9,0,0,162);
        }
        bool lit=ht_rain_window_lit(g->ticks);
        ht_rect(ht_scene,wx-22,roof-70,42,53,244);
        ht_rect(ht_scene,wx-18,roof-66,34,45,lit?34:164);
        ht_rect(ht_scene,wx-16,roof-64,30,39,lit?69:183);
        ht_rect(ht_scene,wx-26,roof-17,51,4,lit?38:147);
        ht_limb(ht_scene,wx-1,roof-67,wx-1,roof-21,1,1,225);
        ht_limb(ht_scene,wx-18,roof-46,wx+16,roof-46,1,1,225);
        ht_checkpoint();
    }
    /* Bounded rain sheets in this court only; stop beneath the tank belly.
     * World coordinates and simulation ticks make identical views stable. */
    int left=ht_max(1735,cx-margin),right=ht_min(2205,cx+HT_W+margin);
    for(int i=0;i<52;++i) {
        int wx=1735+(int)((i*71u+g->ticks*2u)%470u);
        if(wx<left || wx>right)continue;
        int wy=-208+(int)((i*47u+g->ticks*7u)%306u);
        int bottom=ht_rain_tank_weather_floor(g,wx-cx,wx-cx-2,wy-cy+9);
        if(wy-cy<bottom)ht_limb(ht_scene,wx-cx,wy-cy,wx-cx-2,bottom-1,0,0,119);
        if((i&15)==15)ht_checkpoint();
    }
}
