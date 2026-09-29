/* Authored landforms, shared by feet, loose bodies, weather and silhouettes.
 * A chapter is not a row of suspended decks. Only actual movable spans and
 * service gantries have thin undersides. All heights use bounded integer math. */
enum { HT_EARTH,HT_ROOF,HT_SAND,HT_BALLAST,HT_MUD,HT_ROCK,HT_GARDEN,HT_MASONRY,HT_SNOW,HT_TOWER,HT_DECK };
static int ht_terrain_kind(unsigned level,int i) {
    if(i==ht_mechanics_by_level[level].bridge || (level==2 && i==3) || (level==3 && (i==3 || i==8))) return HT_DECK;
    return (int)level;
}
typedef struct {int16_t center,radius,height;} ht_landform;
/* Broad, unequal contours. Flat work areas around machinery are intentional;
 * natural routes join at grade except at authored ravines and channels. */
static const ht_landform ht_landforms[HT_LEVELS][HT_PLATFORMS]={
    {{85,95,26},{0,0,0},{875,170,33},{1170,90,16},{1365,70,14},{1760,155,30},{2180,80,16},{2380,100,25},{2630,135,34},{2822,42,5}},
    {{0,0,0}},
    {{180,165,12},{714,100,9},{972,60,6},{0,0,0},{0,0,0},{0,0,0},{2180,160,17},{2450,75,8},{2660,80,10},{0,0,0}},
    {{220,175,8},{745,95,6},{1080,100,7},{1360,100,6},{1770,110,7},{2010,90,5},{2250,115,6},{0,0,0},{0,0,0},{0,0,0}},
    {{155,130,9},{820,110,8},{1135,110,12},{1390,105,7},{1640,100,14},{1960,110,9},{2240,90,7},{2490,115,11},{2710,75,7},{0,0,0}},
    {{0,0,0},{0,0,0},{880,75,9},{1150,80,12},{1450,100,9},{1720,75,5},{1990,110,14},{2325,75,10},{2640,115,12},{0,0,0}},
    {{170,130,17},{665,105,8},{930,60,5},{1210,90,14},{1530,120,22},{1850,95,11},{2100,90,6},{2410,95,17},{2660,75,12},{0,0,0}},
    {{0,0,0}},
    {{180,155,23},{658,70,19},{910,95,26},{1220,70,15},{1530,75,8},{1790,105,24},{2110,80,18},{2370,105,30},{2690,100,24},{0,0,0}},
    {{120,100,8},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}
};
static int ht_land_height(unsigned level,int i,int x) {
    const ht_landform *f=&ht_landforms[level][i];
    if(!f->radius) return ht_level_land(level)[i].top;
    int u=ht_abs(x-f->center)*256/f->radius;
    if(u>=256) return ht_level_land(level)[i].top;
    /* Squared parabola: horizontal tangents at crest and both joins. */
    int bell=256-u*u/256;
    const ht_platform *p=&ht_level_land(level)[i];
    int edge=ht_clamp(ht_min(x-p->left-16,p->right-16-x)*8,0,256);
    edge=edge*edge/256*(768-2*edge)/256;
    return p->top-(f->height*bell*bell/65536)*edge/256;
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
