/* Authored landforms, shared by feet, loose bodies, weather and silhouettes.
 * A chapter is not a row of suspended decks. Only actual movable spans and
 * service gantries have thin undersides. All heights use bounded integer math. */
enum { HT_EARTH,HT_ROOF,HT_SAND,HT_BALLAST,HT_MUD,HT_ROCK,HT_GARDEN,HT_MASONRY,HT_SNOW,HT_TOWER,HT_DECK };
static int ht_terrain_kind(unsigned level,int i) {
    if(i==ht_mechanics_by_level[level].bridge || (level==2 && i==3) || (level==3 && (i==3 || i==8))) return HT_DECK;
    return (int)level;
}
typedef struct {int16_t x,offset;} ht_contour;
/* World-space survey points, not one bell per collision rectangle. Adjacent
 * parcels share a contour; only authored cliffs/channels break the landmass.
 * Zero-height work pads preserve load/plate, rope and gate contacts. */
static const ht_contour ht_contours[HT_LEVELS][32]={
    {{-160,-18},{15,-25},{100,-8},{210,0},{380,0},{670,0},{710,-5},{835,-52},{960,-45},{1080,-28},{1180,-16},{1310,-39},{1400,-12},{1460,0},{1600,0},{1730,-35},{1870,-48},{1980,-16},{2040,0},{2090,0},{2220,-21},{2380,-39},{2530,-18},{2670,-31},{2780,0},{3410,0}},
    {{-160,0},{3410,0}},
    {{-160,-9},{20,-17},{190,-6},{350,-21},{520,0},{580,0},{720,-16},{880,-9},{1020,0},{1485,0},{1980,0},{2140,-24},{2320,-13},{2480,-30},{2670,-12},{2780,0},{3410,0}},
    {{-160,0},{180,-5},{410,-3},{620,0},{890,0},{950,0},{1240,0},{1500,0},{1640,0},{1800,-7},{1990,-3},{2200,-9},{2410,0},{3410,0}},
    {{-160,-12},{20,-6},{170,-18},{300,-4},{350,0},{680,0},{810,-12},{1000,-5},{1180,-21},{1370,-8},{1520,-17},{1690,-5},{1790,0},{1840,0},{2020,-13},{2210,-7},{2430,-20},{2610,-9},{2800,0},{3410,0}},
    {{-160,0},{690,0},{720,0},{850,-14},{930,-8},{1050,0},{1120,-18},{1260,0},{1390,-12},{1510,-20},{1610,0},{1720,-9},{1830,0},{1970,-24},{2140,0},{2340,-18},{2460,0},{2640,-22},{2790,0},{3410,0}},
    {{-160,-8},{100,-25},{250,0},{550,0},{650,-14},{820,0},{1020,0},{1180,-19},{1330,0},{1530,-27},{1690,0},{1850,-12},{1990,0},{2140,-10},{2230,0},{2420,-23},{2520,0},{2680,-15},{2780,0},{3410,0}},
    {{-160,0},{3410,0}},
    {{-160,-12},{60,-34},{220,-15},{340,-7},{410,0},{550,0},{680,-24},{800,0},{920,-35},{1070,0},{1230,-20},{1350,0},{1440,0},{1580,0},{1690,0},{1810,-33},{1940,0},{2130,-29},{2250,0},{2370,-39},{2510,0},{2670,-28},{2820,0},{3410,0}},
    {{-160,0},{3410,0}}
};
static int ht_land_height(unsigned level,int i,int x) {
    int top=ht_level_land(level)[i].top;
    if(ht_terrain_kind(level,i)==HT_DECK) return top;
    const ht_contour *points=ht_contours[level];
    for(int n=1;n<32 && points[n].x>points[n-1].x;++n) {
        if(x>points[n].x) continue;
        int span=points[n].x-points[n-1].x;
        int u=ht_clamp((x-points[n-1].x)*256/span,0,256);
        /* Broad earth transitions; rock keeps its fractured angular profile. */
        if(level!=5 && level!=8) u=u*u/256*(768-2*u)/256;
        return top+points[n-1].offset+(points[n].offset-points[n-1].offset)*u/256;
    }
    return top;
}
static int ht_platform_top(const ht_game *g,int i) {
    int top=ht_level_land(g->level)[i].top;
    if(i==ht_mechanics_by_level[g->level].bridge) top-=(32-g->traversal.bridge_open)*HT_BRIDGE_STEP;
    return top;
}
static int ht_surface_at(const ht_game *g,int i,int x) {
    return ht_land_height(g->level,i,x)+ht_platform_top(g,i)-ht_level_land(g->level)[i].top;
}
/* A ladder approach beneath an overlapping upper route is a real recess in
 * the cliff/building. Its ceiling and back wall exist in both art and physics. */
static int ht_terrain_bottom(const ht_game *g,int i,int x) {
    if(ht_terrain_kind(g->level,i)==HT_DECK) return ht_surface_at(g,i,x)+18;
    const ht_platform *land=ht_level_land(g->level);
    if(i>0 && land[i-1].top>land[i].top+40 && x<land[i-1].right+8) {
        const ht_mechanics *m=&ht_mechanics_by_level[g->level];
        for(int ladder=0;ladder<3;++ladder)
            if(m->ladders[ladder].x && m->ladders[ladder].upper==i && m->ladders[ladder].lower==i-1)
                return ht_surface_at(g,i,x)+20;
    }
    if(i+1<HT_PLATFORMS && land[i+1].top>land[i].top+40 && x>=land[i+1].left-8) return ht_surface_at(g,i,x)+20;
    return 520;
}
