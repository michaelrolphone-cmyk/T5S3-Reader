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
    {{-160,-18},{15,-25},{100,-8},{210,0},{258,0},{288,29},{307,29},{340,0},{380,0},{670,0},{710,-5},{835,-52},{960,-45},{1080,-28},{1180,-16},{1310,-39},{1400,-12},{1460,0},{1560,0},{1730,-35},{1870,-48},{1980,-16},{2058,0},{2082,0},{2220,-21},{2380,-39},{2530,-18},{2670,-31},{2780,0},{3410,0}},
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
    if(level==3 && i==3 && x<HT_TRESTLE_CAP)return HT_TRESTLE_FLOOR;
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
/* The fallen snag keeps the same shallow bend in art and footing. */
#define HT_FOREST_PUSH_STEPS 48 /* 1.536 seconds of continuous loaded effort. */
static int ht_forest_log_sine(int angle) {
    static const int16_t sine[17]={0,25,50,74,98,121,142,162,181,198,213,226,237,245,251,255,256};
    int node=ht_min(15,angle/256),u=angle-node*256;
    return sine[node]+((sine[node+1]-sine[node])*u+128)/256;
}
static int ht_forest_log_angle(const ht_game *g) {
    if(g->traversal.forest_log_phase==32)return 4096;
    if(g->traversal.forest_log_falling)return g->traversal.forest_log_angle;
    int effort=g->traversal.forest_push;
    return 124+effort*effort*96/(HT_FOREST_PUSH_STEPS*HT_FOREST_PUSH_STEPS);
}
static const int8_t ht_forest_log_curve[5]={0,-4,-7,-3,0};
/* Shared trunk sections keep blocking volume aligned with the rendered lean. */
static void ht_forest_log_node(int dx,int dy,int i,int *x,int *y,int *radius,int *bottom) {
    int q=ht_min(3,i/8),u=i-q*8;
    int bend=ht_forest_log_curve[q]+(ht_forest_log_curve[q+1]-ht_forest_log_curve[q])*u/8;
    *x=374+dx*i/32-dy*bend/312;*y=220+dy*i/32+dx*bend/312;
    *radius=12-i/8;*bottom=*radius+(i?(int)(ht_hash((unsigned)i+571)%3):0);
}

static int ht_platform_top(const ht_game *g,int i) {
    int top=ht_level_land(g->level)[i].top;
    if(g->level==0 && i==1)return 208;
    if(i==ht_mechanics_by_level[g->level].bridge) top-=(32-g->traversal.bridge_open)*HT_BRIDGE_STEP;
    return top;
}
static int ht_surface_at(const ht_game *g,int i,int x) {
    if(g->level==0 && i==1) {
        int at=ht_clamp(x-374,0,312),node=ht_min(3,at/78),u=at-node*78;
        int bend=ht_forest_log_curve[node]+(ht_forest_log_curve[node+1]-ht_forest_log_curve[node])*u/78;
        return 220+bend-12+node+u/78;
    }
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

/* Exposed natural walls recede in strata below an intact walkable lip. Shared
 * by raster and body collision; never carve machinery sockets or ladder bays. */
static bool ht_cliff_edge(const ht_game *g,int i,int side) {
    int kind=ht_terrain_kind(g->level,i);
    if(kind!=HT_EARTH && kind!=HT_SAND && kind!=HT_MUD && kind!=HT_ROCK && kind!=HT_SNOW) return false;
    if(i==ht_mechanics_by_level[g->level].pit_platform) return false;
    const ht_platform *land=ht_level_land(g->level),*p=&land[i];
    int edge=side<0?p->left:p->right;
    if(ht_terrain_bottom(g,i,edge-side)!=520) return false;
    for(int j=0;j<HT_PLATFORMS;++j) if(j!=i && ht_terrain_kind(g->level,j)!=HT_DECK &&
        land[j].left<=edge && land[j].right>=edge) return false;
    return true;
}
static int ht_cliff_inset(const ht_game *g,int i,int side,int y) {
    static const int16_t depth[]={0,8,29,53,87,128,183,260,520};
    static const int8_t profiles[3][9]={
        {0,0,6,23,14,31,25,42,34}, /* undercut rooted earth */
        {0,0,11,7,26,17,36,29,43}, /* fractured stone */
        {0,0,5,13,20,16,29,38,45}  /* washed sand/silt */
    };
    const ht_platform *p=&ht_level_land(g->level)[i];
    int edge=side<0?p->left:p->right-1;
    int d=y-ht_surface_at(g,i,edge),kind=ht_terrain_kind(g->level,i);
    if(d<=8) return 0;
    const int8_t *profile=profiles[(kind==HT_ROCK || kind==HT_SNOW)?1:(kind==HT_SAND || kind==HT_MUD)?2:0];
    for(int n=1;n<9;++n) if(d<=depth[n]) {
        int a=profile[n-1],b=profile[n];
        int v=a+(b-a)*(d-depth[n-1])/(depth[n]-depth[n-1]);
        /* Opposite banks have unequal erosion, not mirrored cutouts. */
        return ht_min((p->right-p->left)/4,v*(3+(i+(side>0))%3)/4);
    }
    return ht_min((p->right-p->left)/4,profile[8]);
}
static void ht_cliff_bounds(const ht_game *g,int i,int head,int feet,int *l,int *r) {
    /* At most 34 rows for current player/loose bodies. Find the outermost
     * rock intersecting the body's vertical extent, including stratum corners. */
    int left=520,right=520;
    for(int y=head;y<=feet;++y) {
        left=ht_min(left,ht_cliff_inset(g,i,-1,y));
        right=ht_min(right,ht_cliff_inset(g,i,1,y));
    }
    if(ht_cliff_edge(g,i,-1)) *l+=left;
    if(ht_cliff_edge(g,i,1)) *r-=right;
}

/* Grotto underside is authored once for the roof silhouette and head contact. */
static const int16_t ht_grotto_roof[][2]={
    {-300,175},{-80,166},{90,160},{170,154},{210,144},
    {265,140},{300,124},{348,112},{366,91},{385,85},
    {406,62},{435,55},{460,30},{475,-20},{490,-100},{520,-280}
};
static int ht_grotto_ceiling(int x) {
    for(unsigned n=1;n<sizeof(ht_grotto_roof)/sizeof(ht_grotto_roof[0]);++n)
        if(x<=ht_grotto_roof[n][0]) {
            int a=ht_grotto_roof[n-1][0],b=ht_grotto_roof[n][0];
            int top=ht_grotto_roof[n-1][1],end=ht_grotto_roof[n][1];
            return top+(end-top)*ht_clamp(x-a,0,b-a)/(b-a);
        }
    return -520;
}

/* Bury a rigid scenery footprint beneath its deepest soil contact. Drawing
 * the actual land afterward masks the buried part, including sloping ground. */
static int ht_buried_base(const ht_game *g,int i,int world,int radius) {
    const ht_platform *p=&ht_level_land(g->level)[i];
    int bottom=ht_surface_at(g,i,world);
    for(int x=ht_max(p->left,world-radius);x<=ht_min(p->right-1,world+radius);++x)
        bottom=ht_max(bottom,ht_surface_at(g,i,x));
    return bottom+3;
}
