/* Seven authored environments. The boat, forest and city never enter this
 * path. Surveyed ridge lines replace repeated scenery cells; distant objects
 * are rooted in those ridges. All coordinates remain world anchored. */
static bool ht_authored_chapter(unsigned level) {return level>=2 && level!=4;}
typedef struct {
    int16_t light_x,light_y;
    uint8_t rates[3];
    int16_t ridge[3][13];
} ht_chapter_composition;
/* Positional entries also compile in the C++ host math/render harness. */
static const ht_chapter_composition ht_chapter_compositions[HT_LEVELS]={
    {0,0,{0,0,0},{{0},{0},{0}}}, /* Forest has its own composition. */
    {0,0,{0,0,0},{{0},{0},{0}}}, /* City has its own composition. */
    {390,-55,{15,33,56},{
        {155,142,70,48,62,127,146,91,80,115,152,132,156},
        {202,184,139,116,153,202,155,102,137,185,166,139,198},
        {255,238,213,199,229,251,218,189,207,240,228,219,248}}},
    {465,-100,{17,36,60},{
        {122,88,130,69,81,136,112,54,98,143,123,79,119},
        {184,155,202,171,126,163,211,160,151,186,214,160,194},
        {258,239,221,244,261,248,224,236,260,242,217,238,255}}},
    {0,0,{0,0,0},{{0},{0},{0}}}, /* Preserve the boat composition. */
    {315,-90,{14,32,57},{
        {90,78,14,22,61,48,-28,-15,55,70,24,41,102},
        {145,129,101,46,59,123,152,97,51,79,138,161,120},
        {236,218,191,205,178,212,227,178,143,188,222,184,214}}},
    {400,-65,{16,35,59},{
        {164,143,92,105,159,127,79,105,164,129,100,134,156},
        {220,191,170,189,207,163,151,178,212,183,169,191,216},
        {263,241,227,235,249,220,211,230,256,240,219,243,258}}},
    {405,-170,{13,31,54},{
        {99,45,-71,10,120,104,-33,-93,22,133,77,-11,85},
        {166,114,37,105,192,169,85,32,127,211,163,76,146},
        {245,204,170,214,268,244,189,161,218,270,241,192,243}}},
    {350,-130,{12,29,53},{
        {54,-42,-156,-65,44,-80,-191,-83,18,-118,-178,-55,30},
        {144,70,-39,57,127,13,-83,31,99,-7,-64,65,150},
        {235,185,91,172,226,137,55,142,211,108,81,190,240}}},
    {455,-155,{15,34,58},{
        {150,109,67,89,129,61,30,78,141,100,49,77,140},
        {201,163,140,176,188,118,93,146,207,151,129,163,198},
        {255,222,191,220,244,187,158,198,247,214,187,219,256}}}
};
static int ht_chapter_ridge(unsigned level,int plane,int x) {
    int node=ht_clamp(ht_floor_div(x+600,300),0,11);
    int u=ht_clamp((x+600-node*300)*256/300,0,256);
    if(level==2 || level==3 || level==6 || level==9)u=u*u/256*(768-2*u)/256;
    const int16_t *v=ht_chapter_compositions[level].ridge[plane];
    return v[node]+(v[node+1]-v[node])*u/256;
}
static void ht_authored_pump(int x,int y,int tone) {
    ht_limb(ht_scene,x-29,y,x-5,y-67,4,3,tone);
    ht_limb(ht_scene,x+24,y,x-5,y-67,4,3,tone);
    ht_limb(ht_scene,x-58,y-83,x+61,y-63,5,6,tone);
    static const ht_foreground_section head[]={
        {55,-76,65,-73},{59,-70,71,-66},{61,-62,74,-58},
        {62,-54,74,-49},{61,-45,71,-40},{58,-39,66,-35}
    };
    ht_foreground_contour(x,y,head,sizeof(head)/sizeof(head[0]),tone);
    ht_limb(ht_scene,x+68,y-42,x+68,y,0,0,tone);
    ht_disk(ht_scene,x-44,y-15,14,tone);
    ht_limb(ht_scene,x-44,y-15,x-39,y-80,2,2,tone);
    ht_rect(ht_scene,x-65,y-3,143,6,tone);
}
static void ht_authored_glasshouse(int x,int y,int w,int h,int tone) {
    /* Continuous curved glazing ribs, with panes missing near the crown. */
    int previous_x=x,previous_y=y-24;
    for(int k=1;k<=16;++k) {
        int u=k*16,nx=x+w*k/16,ny=y-24-h*4*u*(256-u)/65536;
        ht_limb(ht_scene,previous_x,previous_y,nx,ny,1,1,tone);
        if(k%2==0)ht_limb(ht_scene,nx,ny,nx,y,1,1,tone);
        previous_x=nx;previous_y=ny;
    }
    ht_limb(ht_scene,x,y-24,x+w,y-24,1,1,tone);
    ht_rect(ht_scene,x,y-3,w,7,tone);
    ht_limb(ht_scene,x+w/3,y-7,x+w/3-9,y-h/2,2,0,tone-12);
    ht_leaf_cluster(ht_scene,x+w/3-9,y-h/2,12,871u,tone-12);
}
static void ht_authored_landmarks(const ht_game *g) {
    unsigned level=g->level;
    const ht_chapter_composition *c=&ht_chapter_compositions[level];
    int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
    /* Four deliberately spaced sites, each with a fixed footprint on its bank. */
    static const int sites[4]={95,620,1210,1860};
    for(int i=0;i<4;++i) {
        int wx=sites[i],x=wx-cx*c->rates[1]/100;
        if(x+255< -margin || x-160>HT_W+margin)continue;
        int foundation=ht_chapter_ridge(level,1,wx);
        for(int foot=-80;foot<=160;foot+=20)
            foundation=ht_max(foundation,ht_chapter_ridge(level,1,wx+foot));
        int y=foundation-cy*c->rates[1]/100+7;
        int tone=140;
        if(level==2) {
            if(i%2==0)ht_authored_pump(x,y,tone);
            else {
                ht_rect(ht_scene,x-35,y-47,70,48,tone);
                ht_disk(ht_scene,x,y-47,35,tone);
                ht_rect(ht_scene,x-35,y-46,70,46,tone);
                ht_limb(ht_scene,x-20,y-65,x+20,y-65,1,1,tone-22);
                ht_limb(ht_scene,x+35,y-8,x+93,y-8,4,4,tone);
            }
        } else if(level==3) {
            ht_rect(ht_scene,x-70,y-48,210,48,tone);
            ht_triangle(ht_scene,x-80,y-48,x+20,y-85,x+151,y-48,tone);
            for(int door=0;door<3;++door)ht_rect(ht_scene,x-48+door*59,y-28,37,28,tone+16);
            ht_limb(ht_scene,x-110,y+2,x+184,y+2,1,1,tone);
            ht_limb(ht_scene,x+158,y,x+158,y-90,2,1,tone);
            ht_rect(ht_scene,x+150,y-91,17,26,tone);
            ht_disk(ht_scene,x+158,y-83,3,62);
        } else if(level==5) {
            /* Sparse abandoned cranes planted on the bench, not floating rails. */
            if(i%2) {
                ht_limb(ht_scene,x-21,y,x-14,y-130,5,3,tone);
                ht_limb(ht_scene,x+21,y,x-14,y-130,5,3,tone);
                ht_limb(ht_scene,x-43,y-131,x+113,y-112,3,2,tone);
                ht_limb(ht_scene,x-14,y-151,x+113,y-112,1,1,tone);
                ht_limb(ht_scene,x+104,y-113,x+104,y-48,0,0,tone);
            }
        } else if(level==6)ht_authored_glasshouse(x-80,y,240,105+i%2*35,tone);
        else if(level==7) {
            /* Fine aqueduct through the distant valley; the main dam is unique. */
            ht_rect(ht_scene,x-100,y-35,280,6,tone);
            for(int pier=0;pier<5;++pier)ht_rect(ht_scene,x-91+pier*59,y-30,7,38,tone);
        } else if(level==8) {
            /* Wind-eroded snow facets follow the authored mountain face. */
            int roof=ht_chapter_ridge(level,1,wx-45)-cy*c->rates[1]/100;
            ht_triangle(ht_scene,x-45,roof,x-9,y-4,x+55,y+49,104);
            ht_limb(ht_scene,x-9,y-4,x+36,y+70,1,0,162);
        } else {
            ht_rect(ht_scene,x-32,y-37,92,37,tone);
            ht_triangle(ht_scene,x-43,y-37,x+12,y-69,x+72,y-37,tone);
            ht_rect(ht_scene,x+40,y-65,8,29,tone);
            for(int win=0;win<3;++win)ht_rect(ht_scene,x-21+win*26,y-26,9,14,48);
        }
        ht_checkpoint();
    }
    if(level==9) {
        /* One radio mast, rising above the settlement as the final approach opens. */
        int wx=1730,x=wx-cx*c->rates[2]/100;
        int y=ht_chapter_ridge(level,2,wx)-cy*c->rates[2]/100;
        if(x> -180-margin && x<HT_W+180+margin) {
            ht_limb(ht_scene,x-37,y,x-3,y-360,4,2,194);
            ht_limb(ht_scene,x+37,y,x+3,y-360,4,2,194);
            for(int k=1;k<9;++k) {
                int top=y-360+k*40,w=k*4;
                ht_limb(ht_scene,x-w,top,x+w,top,1,1,184);
                ht_limb(ht_scene,x-w,top,x+w-4,top-40,1,1,184);
            }
            ht_limb(ht_scene,x,y-290,x-142,y,0,0,155);
            ht_limb(ht_scene,x,y-290,x+142,y,0,0,155);
            ht_disk(ht_scene,x,y-363,3,62);
        }
    }
}
static void ht_authored_backdrop(const ht_game *g) {
    if(!ht_authored_chapter(g->level))return;
    const ht_chapter_composition *c=&ht_chapter_compositions[g->level];
    int cx=g->camera/256,cy=g->camera_y/256;
    uint8_t *cached=ht_cached_near[2][0];
    if(ht_forest_frame_valid && ht_composed_frame_level==g->level && ht_forest_frame_x==cx &&
       ht_forest_frame_y==cy && ht_forest_frame_scale==ht_world_scale) {
        memcpy(ht_scene,cached,HT_PIXELS);return;
    }
    ht_forest_frame_valid=false;
    static int16_t tops[3][HT_W];
    static int light_distance[HT_W/2];
    int lx=ht_project_x(c->light_x-cx*c->rates[0]/100);
    int ly=ht_project_y(c->light_y-cy*c->rates[0]/100);
    for(int x=0;x<HT_W;++x) {
        int wx=240+(x-240)*256/ht_world_scale;
        for(int p=0;p<3;++p)tops[p][x]=(int16_t)ht_project_y(
            ht_chapter_ridge(g->level,p,wx+cx*c->rates[p]/100)-cy*c->rates[p]/100);
        if(!(x&1)){int dx=x-lx;light_distance[x/2]=dx*dx;}
    }
    for(int y=0;y<HT_H;y+=2) {
        uint8_t *row=ht_scene+y*HT_W;int dy=y-ly;
        for(int x=0;x<HT_W;x+=2) {
            int glow=ht_min(52,(light_distance[x/2]+dy*dy/2)/5500);
            int ink=18+glow;
            for(int p=2;p>=0;--p)if(y>=tops[p][x]) {
                int depth=y-tops[p][x];
                ink=72+p*43+glow/3+ht_min(17,depth/9);
                /* Quarry strata remain attached to one continuous face. */
                if(g->level==5 && depth>16 && depth%39<2)ink-=14;
                break;
            }
            row[x]=row[x+1]=(uint8_t)ink;
        }
        memcpy(row+HT_W,row,HT_W);
        if((y&15)==14)ht_checkpoint();
    }
    ht_authored_landmarks(g);
    /* The near bank occludes middle-distance footings, instead of letting
     * machinery overwrite a nearer ridge. Skip the untouched upper sky. */
    int first=HT_H;
    for(int x=0;x<HT_W;x+=2)first=ht_min(first,tops[2][x]);
    for(int y=ht_max(0,first)&~1;y<HT_H;y+=2) {
        uint8_t *row=ht_scene+y*HT_W;int dy=y-ly;
        for(int x=0;x<HT_W;x+=2)if(y>=tops[2][x]) {
            int depth=y-tops[2][x];
            int glow=ht_min(52,(light_distance[x/2]+dy*dy/2)/5500);
            int ink=158+glow/3+ht_min(17,depth/9);
            if(g->level==5 && depth>16 && depth%39<2)ink-=14;
            row[x]=row[x+1]=row[HT_W+x]=row[HT_W+x+1]=(uint8_t)ink;
        }
        if((y&15)==14)ht_checkpoint();
    }
    if(!ht_abort) {
        memcpy(cached,ht_scene,HT_PIXELS);ht_forest_frame_valid=true;
        ht_composed_frame_level=g->level;ht_forest_frame_x=cx;
        ht_forest_frame_y=cy;ht_forest_frame_scale=ht_world_scale;
    }
}
/* Two close masses per chapter. Their contours and scale match materials;
 * all are on the observer's side, separate from the player's physical route. */
static void ht_authored_foreground(const ht_game *g) {
    if(!ht_authored_chapter(g->level))return;
    int cx=g->camera/256*5/4,cy=g->camera_y/256*5/4,margin=ht_view_margin();
    static const int sites[2]={1100,2980};
    static const ht_foreground_section stone[]={
        {-39,-460,15,-460},{-35,-370,31,-370},{-44,-304,38,-304},
        {-31,-250,35,-250},{-29,-209,18,-209},{-17,-172,27,-172},
        {-25,-128,32,-128},{-34,-78,44,-78},{-39,-24,40,-24},
        {-47,20,49,20},{-58,65,57,65},{-60,520,59,520}
    };
    for(int n=0;n<2;++n) {
        int x=sites[n]-cx,y=230-cy;
        if(x< -130-margin || x>HT_W+130+margin)continue;
        if(g->level==5 || g->level==8) {
            ht_foreground_contour(x,y,stone,sizeof(stone)/sizeof(stone[0]),248);
            ht_limb(ht_scene,x+21,y-292,x+6,y-239,1,0,g->level==8?150:213);
            ht_limb(ht_scene,x+6,y-239,x+16,y-206,0,0,210);
        } else if(g->level==6) {
            ht_limb(ht_scene,x,y+180,x-9,y-410,5,4,248);
            ht_limb(ht_scene,x-9,y-410,x+118,y-332,4,3,248);
            ht_triangle(ht_scene,x+9,y-397,x+72,y-358,x+21,y-333,182);
            ht_limb(ht_scene,x-5,y-174,x-19,y-119,2,1,240);
            for(int k=0;k<3;++k)ht_leaf_cluster(ht_scene,x-16,y-163+k*18,13,912u+k*17,244);
        } else if(g->level==2 || g->level==7) {
            ht_limb(ht_scene,x,y+260,x,y-325,13,13,247);
            ht_limb(ht_scene,x,y-325,x+73,y-338,13,10,247);
            ht_disk(ht_scene,x,y-325,13,247);
            for(int k=0;k<3;++k)ht_rect(ht_scene,x-18,y-245+k*141,36,8,250);
            ht_limb(ht_scene,x+7,y-300,x+7,y+200,1,1,196);
        } else {
            ht_rect(ht_scene,x-13,y-500,26,790,247);
            ht_rect(ht_scene,x-20,y-178,40,8,249);
            ht_limb(ht_scene,x,y-232,x+75,y-232,4,3,247);
            ht_limb(ht_scene,x+75,y-232,x+134,y-219,0,0,218);
            ht_rect(ht_scene,x-19,y-310,38,55,249);
            if(g->level==3)ht_disk(ht_scene,x,y-292,7,165);
        }
        ht_checkpoint();
    }
}
/* Fixed alpine safety lines use the existing deliberate Up/Down climbing
 * controls. Bolts are sunk into stone; there are no ladders on snow piles. */
static void ht_alpine_line(int x,int top,int bottom) {
    ht_limb(ht_scene,x-3,top,x-1,bottom,1,1,235);
    ht_limb(ht_scene,x+3,top,x+1,bottom,0,0,165);
    for(int y=top;y<=bottom;y+=32) {
        ht_disk(ht_scene,x,y,3,245);ht_disk(ht_scene,x,y,1,95);
        ht_limb(ht_scene,x-5,y+2,x+5,y+2,0,0,230);
    }
}
