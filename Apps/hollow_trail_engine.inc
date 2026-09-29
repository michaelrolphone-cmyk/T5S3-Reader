/* Hollow Trail — deterministic ten-chapter platformer and PSRAM raster pipeline.
 * World/overlays are 480x270; scenery is 240x135; output is 960x540. No textures, random frame noise,
 * floating-point library, framebuffer reads, or per-frame allocation. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "T5MathApi.h"
#include "epd_dither.h"
#define HT_W 480
#define HT_H 270
#define HT_SCENE_W (HT_W/2)
#define HT_SCENE_H (HT_H/2)
#define HT_SCENE_PIXELS (HT_SCENE_W*HT_SCENE_H)
/* Physical pixels are 2x logical: 18px black, fading through 38px. */
#define HT_BORDER 9
#define HT_FADE 19
#define HT_MASK_RADIUS 45
/* Keep the following low-resolution scene aligned for four-byte loads. */
#define HT_MASK_PIXELS ((HT_MASK_RADIUS*HT_MASK_RADIUS+15)&~15)
static bool ht_framed;
static uint16_t ht_dirty_top,ht_dirty_height;
#define HT_PIXELS (HT_W * HT_H)
#define HT_TILE_W 256
#define HT_TILE_SLOTS 4
#define HT_LAYERS 4
#define HT_TILE_PIXELS (HT_TILE_W * HT_H)
#define HT_TILE_HALO ((HT_W-HT_TILE_W)/2)
#define HT_CACHE_BAND 64
#define HT_WARM_STEPS (HT_LAYERS*HT_TILE_SLOTS*(1+3*((HT_H+HT_CACHE_BAND-1)/HT_CACHE_BAND)))
#define HT_MEMORY (HT_SCENE_PIXELS * 4u + HT_PIXELS * 6u + HT_LAYERS * HT_TILE_SLOTS * HT_TILE_PIXELS * 2u + HT_MASK_PIXELS + HT_SCENE_PIXELS)
#define HT_GOAL 3200
#define HT_STEP_MS 32u /* Half-speed motion; preserve jump paths and level reachability. */

typedef struct { int left, right, top; } ht_platform;
static const ht_platform ht_forest[] = {
    { -160, 390, 222 }, { 462, 725, 207 }, { 804, 1050, 190 },
    { 1118, 1300, 216 }, { 1374, 1535, 192 }, { 1610, 1800, 173 },
    { 1865, 2130, 208 }, { 2210, 2415, 188 }, { 2490, 2710, 214 },
    { 2780, 3410, 200 }
};
static const ht_platform ht_city[] = {
    { -160, 365, 217 }, { 433, 690, 193 }, { 763, 1035, 164 },
    { 1105, 1315, 190 }, { 1380, 1570, 163 }, { 1635, 1835, 138 },
    { 1905, 2130, 173 }, { 2198, 2430, 150 }, { 2498, 2730, 179 },
    { 2798, 3410, 155 }
};
#define HT_PLATFORMS 10
static const ht_platform ht_oil[] = {
    { -160, 380, 222 }, { 448, 710, 206 }, { 780, 1040, 184 },
    { 1108, 1330, 209 }, { 1398, 1580, 183 }, { 1648, 1845, 159 },
    { 1913, 2140, 190 }, { 2208, 2435, 170 }, { 2503, 2730, 203 },
    { 2798, 3410, 200 }
};
#define HT_LEVELS 10
static const ht_platform ht_routes[7][HT_PLATFORMS] = {
    { { -160, 380, 222 }, { 448, 710, 204 }, { 784, 1044, 180 }, { 1104, 1326, 203 }, { 1398, 1580, 175 }, { 1652, 1849, 158 }, { 1909, 2136, 187 }, { 2208, 2435, 165 }, { 2507, 2734, 196 }, { 2798, 3410, 200 } },
    { { -160, 380, 219 }, { 452, 714, 201 }, { 776, 1036, 177 }, { 1108, 1330, 209 }, { 1402, 1584, 181 }, { 1644, 1841, 155 }, { 1913, 2140, 184 }, { 2212, 2439, 162 }, { 2499, 2726, 202 }, { 2798, 3410, 197 } },
    { { -160, 380, 216 }, { 444, 706, 198 }, { 780, 1040, 183 }, { 1112, 1334, 206 }, { 1394, 1576, 178 }, { 1648, 1845, 152 }, { 1917, 2144, 190 }, { 2204, 2431, 168 }, { 2503, 2730, 199 }, { 2798, 3410, 194 } },
    { { -160, 380, 222 }, { 448, 710, 204 }, { 784, 1044, 180 }, { 1104, 1326, 203 }, { 1398, 1580, 175 }, { 1652, 1849, 158 }, { 1909, 2136, 187 }, { 2208, 2435, 165 }, { 2507, 2734, 196 }, { 2798, 3410, 200 } },
    { { -160, 380, 219 }, { 452, 714, 201 }, { 776, 1036, 177 }, { 1108, 1330, 209 }, { 1402, 1584, 181 }, { 1644, 1841, 155 }, { 1913, 2140, 184 }, { 2212, 2439, 162 }, { 2499, 2726, 202 }, { 2798, 3410, 197 } },
    { { -160, 380, 216 }, { 444, 706, 198 }, { 780, 1040, 183 }, { 1112, 1334, 206 }, { 1394, 1576, 178 }, { 1648, 1845, 152 }, { 1917, 2144, 190 }, { 2204, 2431, 168 }, { 2503, 2730, 199 }, { 2798, 3410, 194 } },
    { { -160, 380, 222 }, { 448, 710, 204 }, { 784, 1044, 180 }, { 1104, 1326, 203 }, { 1398, 1580, 175 }, { 1652, 1849, 158 }, { 1909, 2136, 187 }, { 2208, 2435, 165 }, { 2507, 2734, 196 }, { 2798, 3410, 200 } }
};
#include "hollow_trail_maps.inc"
/* Fixed-size chapter mechanics, included in every render snapshot. */
enum { HT_FREE, HT_ROLL, HT_CRATE, HT_LADDER, HT_ROPE, HT_BOAT, HT_LEDGE, HT_CLIMB };
typedef struct {
    int ball_x,ball_y,crate_x,boat_x,rope_x,rope_v,ride_offset;
    int ball_vx,ball_vy,ball_roll,crate_y,crate_vx,crate_vy,boat_vx;
    int rope_px[HT_ROPE_NODES],rope_py[HT_ROPE_NODES];
    int rope_ox[HT_ROPE_NODES],rope_oy[HT_ROPE_NODES];
    uint8_t support,bridge_open,fall_ticks;
    uint8_t mode,ladder,ball_phase,ledge,climb,drop_cooldown;
    int ledge_side;
    bool rope_awake;
} ht_traversal;
#include "hollow_trail_story.inc"
#include "hollow_trail_puzzles.inc"
#include "hollow_trail_evidence.inc"
typedef struct {
    int x, y, vx, vy; /* Q8, y is feet */
    int camera, camera_y, checkpoint, coyote, jump_buffer, facing, stride;
    unsigned ticks, laps, deaths, sway_phase;
    int weather_drift; /* Q8, bounded screen-period displacement. */
    unsigned level; /* Laps count complete ten-chapter journeys. */
    int story_x; /* Furthest progress in this chapter, retained on death. */
    bool grounded;
    ht_traversal traversal;
    ht_puzzle_state puzzle;
    uint8_t verdict,last_verdict,endings; /* 0 undecided; 1 break; 2 complete; archive bitmask. */
    bool verdict_read;
    uint8_t story_seen[HT_LEVELS]; /* Reached narration beats; retained with evidence. */
    uint32_t evidence; /* Thirty discoveries, retained across chapters this run. */
} ht_game;
static ht_game ht;
#define ht_land ht_level_land(ht.level)
/* Geometry belongs to a render snapshot, never live input/physics. */
static unsigned ht_geometry_level;
static const t5_math_api_v1 *ht_math;
static int16_t *ht_filter;
static uint8_t *ht_raw, *ht_near, *ht_wide, *ht_temp, *ht_scene;
static uint8_t *ht_cached_near[HT_LAYERS][HT_TILE_SLOTS], *ht_cached_wide[HT_LAYERS][HT_TILE_SLOTS];
static int ht_cache_key[HT_LAYERS][HT_TILE_SLOTS];
static bool ht_cache_valid[HT_LAYERS][HT_TILE_SLOTS];
static struct { bool active; int depth,key,phase,row; } ht_cache_job;
static bool ht_abort;
static unsigned ht_cache_builds;
static uint8_t *ht_corner_mask, *ht_low_scene;
static int16_t ht_visible_left[HT_H],ht_clear_left[HT_H];
/* Host callback polls/yields at bounded raster checkpoints; no drawing. */
static void (*ht_service)(void);
static int ht_min(int a,int b) { return a<b?a:b; }
static int ht_max(int a,int b) { return a>b?a:b; }
static int ht_clamp(int x,int lo,int hi) { return ht_min(hi,ht_max(lo,x)); }
static int ht_floor_div(int x,int n) { int q=x/n; return q-(x%n<0); }
static int ht_abs(int x) { return x<0?-x:x; }
static uint32_t ht_hash(uint32_t x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
static void ht_checkpoint(void) { if(ht_service) ht_service(); }
static void ht_clear_layer(uint8_t *dst) {
    if(!ht_math) { memset(dst,0,HT_PIXELS); return; }
    for(size_t at=0;at<HT_PIXELS;at+=T5_MATH_MAX_BYTES) {
        size_t n=HT_PIXELS-at;
        if(n>T5_MATH_MAX_BYTES) n=T5_MATH_MAX_BYTES;
        (void)ht_math->fill_bytes(dst+at,0,n);
        ht_checkpoint();
    }
}
/* Only used when binding memory, never in a frame's pixel loop. */
static unsigned ht_isqrt(unsigned n) {
    unsigned root=0,bit=1u<<30;
    while(bit>n) bit>>=2;
    while(bit) {
        if(n>=root+bit) { n-=root+bit; root=(root>>1)+bit; }
        else root>>=1;
        bit>>=2;
    }
    return root;
}
static int ht_mask_alpha(int x,int y) {
    int ex=ht_min(x,HT_W-1-x),ey=ht_min(y,HT_H-1-y);
    if(ex<HT_MASK_RADIUS && ey<HT_MASK_RADIUS) return ht_corner_mask[ey*HT_MASK_RADIUS+ex];
    int edge=ht_min(ex,ey);
    return ht_clamp(edge-HT_BORDER,0,HT_FADE-HT_BORDER)*255/(HT_FADE-HT_BORDER);
}
static void ht_mask_init(void) {
    for(int y=0;y<HT_MASK_RADIUS;++y) for(int x=0;x<HT_MASK_RADIUS;++x) {
        int dx=HT_MASK_RADIUS-x,dy=HT_MASK_RADIUS-y;
        int edge=HT_MASK_RADIUS-(int)ht_isqrt((unsigned)(dx*dx+dy*dy));
        ht_corner_mask[y*HT_MASK_RADIUS+x]=(uint8_t)(ht_clamp(edge-HT_BORDER,0,HT_FADE-HT_BORDER)*255/(HT_FADE-HT_BORDER));
    }
    for(int y=0;y<HT_H;++y) {
        int x=0;
        while(x<HT_W/2 && !ht_mask_alpha(x,y)) ++x;
        ht_visible_left[y]=(int16_t)x;
        while(x<HT_W/2 && ht_mask_alpha(x,y)<255) ++x;
        ht_clear_left[y]=(int16_t)x;
    }
    /* The physical packer blends with y+1. Include that preceding row's
     * interpolation halo; excluded rows are permanently solid black. */
    int first=0,last=HT_H-1;
    while(first<HT_H && ht_visible_left[first]==HT_W/2) ++first;
    while(last>first && ht_visible_left[last]==HT_W/2) --last;
    ht_dirty_top=(uint16_t)(2*ht_max(0,first-1));
    ht_dirty_height=(uint16_t)(2*(last+1)-ht_dirty_top);
}
/* Two exact focus maps cover the one-pixel camera-follow alternation without
 * recalculating radial division for every depth or settled frame. */
static uint16_t *ht_focus_map[2];
static int ht_focus_x[2],ht_focus_y[2];
static bool ht_focus_valid[2];
static unsigned ht_focus_recent;
static const uint16_t *ht_focus_weights(int px,int py) {
    for(unsigned i=0;i<2;++i) if(ht_focus_valid[i] && ht_focus_x[i]==px && ht_focus_y[i]==py) {
        ht_focus_recent=i;return ht_focus_map[i];
    }
    unsigned slot=ht_focus_recent^1u;uint16_t *map=ht_focus_map[slot];
    for(int y=0;y<HT_SCENE_H;++y) {
        int dy=2*y-py,distance=px*px+dy*dy,delta=4-4*px;
        for(int x=0;x<HT_SCENE_W;++x) {
            map[y*HT_SCENE_W+x]=(uint16_t)ht_clamp((distance-2500)/200,0,256);
            distance+=delta;delta+=8;
        }
        if((y&15)==15) ht_checkpoint();
    }
    ht_focus_x[slot]=px;ht_focus_y[slot]=py;ht_focus_valid[slot]=true;ht_focus_recent=slot;
    return map;
}
static void ht_pack_tables_init(void);
static void ht_bind(uint8_t *memory) {
    ht_pack_tables_init();
    ht_geometry_level=0; ht_framed=false; ht_abort=false; ht_cache_job.active=false; ht_cache_builds=0;
    ht_raw=memory; ht_near=memory+HT_PIXELS; ht_wide=memory+2*HT_PIXELS;
    ht_scene=memory+3*HT_PIXELS; ht_temp=memory+4*HT_PIXELS; ht_filter=(int16_t *)ht_temp;
    uint8_t *cache=memory+6*HT_PIXELS;
    for(int d=0;d<HT_LAYERS;++d) for(int slot=0;slot<HT_TILE_SLOTS;++slot) {
        ht_cached_near[d][slot]=cache; cache+=HT_TILE_PIXELS;
        ht_cached_wide[d][slot]=cache; cache+=HT_TILE_PIXELS;
    }
    ht_corner_mask=cache; ht_low_scene=cache+HT_MASK_PIXELS; ht_mask_init();
    ht_focus_map[0]=(uint16_t *)(ht_low_scene+HT_SCENE_PIXELS);
    ht_focus_map[1]=ht_focus_map[0]+HT_SCENE_PIXELS;
    ht_focus_valid[0]=ht_focus_valid[1]=false;ht_focus_recent=0;
    memset(ht_cache_valid,0,sizeof(ht_cache_valid));
}
static int ht_evidence_platform(int item) { return 1+item*3; }
static int ht_evidence_x(unsigned level,int item) {
    const ht_platform *p=&ht_level_land(level)[ht_evidence_platform(item)];
    return item==1?p->right-45:p->left+42;
}
static bool ht_evidence_found(const ht_game *game,unsigned page) {
    return page<HT_LEVELS*3u && (game->evidence&(1u<<page))!=0;
}
static int ht_evidence_near(const ht_game *game) {
    if(!game->grounded) return -1;
    for(int item=0;item<3;++item) {
        const ht_platform *p=&ht_level_land(game->level)[ht_evidence_platform(item)];
        if(ht_abs(game->x/256-ht_evidence_x(game->level,item))<=22 &&
           ht_abs(game->y/256-p->top)<=3) return item;
    }
    return -1;
}
static int ht_inspect(void) {
    int item=ht_evidence_near(&ht);
    if(item<0) return -1;
    int page=(int)ht.level*3+item;
    ht.evidence|=1u<<(unsigned)page;
    return page;
}
#define HT_PUZZLE_FIRST 2900
#define HT_PUZZLE_SPACING 85
#define HT_PUZZLE_GATE (HT_GOAL-26)
static int ht_puzzle_near(const ht_game *game) {
    if(!game->grounded) return -1;
    for(int i=0;i<3;++i)
        if(ht_abs(game->x/256-(HT_PUZZLE_FIRST+i*HT_PUZZLE_SPACING))<=22) return i;
    return -1;
}
static bool ht_final_near(const ht_game *game) {
    return game->level==HT_LEVELS-1 && game->puzzle.solved && game->grounded &&
        game->x/256>=HT_PUZZLE_GATE-22 && game->y/256==ht_level_land(game->level)[9].top;
}
static bool ht_final_locked(const ht_game *game) {
    return game->level==HT_LEVELS-1 && (!game->verdict || !game->verdict_read);
}
static bool ht_decide(unsigned choice) {
    if(choice<1 || choice>2 || ht.verdict || !ht_final_near(&ht)) return false;
    ht.verdict=ht.last_verdict=(uint8_t)choice;
    ht.endings|=(uint8_t)(1u<<(choice-1)); ht.verdict_read=false;
    ht.evidence|=1u<<(HT_LEVELS*3-1); // Last document physically changes at the cabinet.
    return true;
}
/* Edge-triggered by app input, also directly exercised by host gameplay tests. */
static bool ht_interact(void) {
    int station=ht_puzzle_near(&ht);
    if(station<0 || ht.puzzle.solved) return false;
    const ht_puzzle_definition *p=&ht_puzzles[ht.level];
    if(p->kind==0) {
        if(station==p->answer[ht.puzzle.progress]) {
            ++ht.puzzle.progress; ht.puzzle.wrong=false;
        } else {
            /* A wrong note starts a fresh attempt; allow it to be note one. */
            ht.puzzle.progress=station==p->answer[0]?1:0; ht.puzzle.wrong=true;
        }
        ht.puzzle.solved=ht.puzzle.progress==p->length;
    } else {
        ht.puzzle.value[station]=(ht.puzzle.value[station]+1)%(p->kind==1?2:4);
        ht.puzzle.solved=true;
        for(int i=0;i<3;++i) if(ht.puzzle.value[i]!=p->answer[i]) ht.puzzle.solved=false;
    }
    return true;
}
/* Gentle gusts: zero at spawn, then a smooth signed pulse, bounded to
 * 0.15625 logical pixels per simulation tick (4.9 px/s). */
static int ht_wind(const ht_game *g) {
    unsigned phase=g->ticks&511u;
    int triangle=phase<128?(int)phase:phase<384?256-(int)phase:(int)phase-512;
    int gust=triangle*(256-ht_abs(triangle))*40/(128*128);
    return g->ticks<96?gust*(int)g->ticks/96:gust;
}
static void ht_spawn(bool restart) {
    uint8_t bridge_open=restart?0:ht.traversal.bridge_open;
    uint32_t evidence=ht.evidence;
    uint8_t verdict=restart?0:ht.verdict,last_verdict=ht.last_verdict,endings=ht.endings;
    bool verdict_read=!restart && ht.verdict_read;
    uint8_t seen[HT_LEVELS]; memcpy(seen,ht.story_seen,sizeof(seen));
    ht_puzzle_state puzzle=ht.puzzle;
    if(restart) memset(&puzzle,0,sizeof(puzzle));
    unsigned laps=ht.laps, deaths=ht.deaths, level=ht.level;
    int cp=restart?0:ht.checkpoint, story_x=restart?0:ht.story_x;
    memset(&ht,0,sizeof(ht)); ht.laps=laps; ht.deaths=deaths; ht.level=level;
    ht.evidence=evidence; ht.puzzle=puzzle;
    ht.verdict=verdict; ht.last_verdict=last_verdict; ht.endings=endings; ht.verdict_read=verdict_read;
    memcpy(ht.story_seen,seen,sizeof(seen));
    if(!ht.story_seen[level]) ht.story_seen[level]=1;
    ht.story_x=story_x; ht.checkpoint=cp; ht.x=(cp?ht_land[cp].left+35:95)*256;
    ht.y=ht_land[cp].top*256; ht.grounded=true; ht.facing=1;
    ht.camera=ht_clamp(ht.x/256-165,0,HT_GOAL-340)*256;
    ht.camera_y=ht_clamp(ht.y/256-180,-300,100)*256;
    const ht_mechanics *m=&ht_mechanics_by_level[level];
    ht.traversal.bridge_open=bridge_open;
    ht.traversal.ball_x=m->ball_x*256;ht.traversal.ball_y=(m->ball_floor-HT_BALL_RADIUS)*256;
    ht.traversal.crate_x=m->crate_x*256;ht.traversal.crate_y=m->crate_floor*256;
    ht.traversal.boat_x=m->boat_left*256;
    for(int i=0;i<HT_ROPE_NODES;++i) {
        ht.traversal.rope_px[i]=ht.traversal.rope_ox[i]=m->rope_anchor_x*256;
        ht.traversal.rope_py[i]=ht.traversal.rope_oy[i]=(m->rope_anchor_y+m->rope_length*i/(HT_ROPE_NODES-1))*256;
    }
}
#include "hollow_trail_traversal.inc"
static void ht_span(uint8_t *b,int x0,int x1,int y,int c) {
    if(y<0 || y>=HT_H || x0>=HT_W || x1<0) return;
    x0=ht_max(x0,0); x1=ht_min(x1,HT_W-1);
    if(x1>=x0) memset(b+y*HT_W+x0,c,(size_t)(x1-x0+1));
}
static void ht_rect(uint8_t *b,int x,int y,int w,int h,int c) {
    for(int row=ht_max(0,y);row<ht_min(HT_H,y+h);++row) ht_span(b,x,x+w-1,row,c);
}
static void ht_triangle(uint8_t *b,int ax,int ay,int bx,int by,int cx,int cy,int c) {
    if(ay>by){int t=ax;ax=bx;bx=t;t=ay;ay=by;by=t;}
    if(by>cy){int t=bx;bx=cx;cx=t;t=by;by=cy;cy=t;}
    if(ay>by){int t=ax;ax=bx;bx=t;t=ay;ay=by;by=t;}
    for(int y=ht_max(ay,0);y<=ht_min(cy,HT_H-1);++y) {
        int a=ax+(cx-ax)*(y-ay)/ht_max(1,cy-ay);
        int d=y<by?ax+(bx-ax)*(y-ay)/ht_max(1,by-ay):bx+(cx-bx)*(y-by)/ht_max(1,cy-by);
        ht_span(b,ht_min(a,d),ht_max(a,d),y,c);
    }
}
static void ht_limb(uint8_t *b,int x,int y,int ex,int ey,int r,int tip,int c) {
    /* Four-corner tapered branch; works for vertical and diagonal limbs. */
    int dx=ex-x,dy=ey-y, len=ht_max(1,ht_max(ht_abs(dx),ht_abs(dy)));
    int nx=-dy*r/len,ny=dx*r/len,tx=-dy*tip/len,ty=dx*tip/len;
    ht_triangle(b,x+nx,y+ny,x-nx,y-ny,ex+tx,ey+ty,c);
    ht_triangle(b,x-nx,y-ny,ex-tx,ey-ty,ex+tx,ey+ty,c);
}
static void ht_disk(uint8_t *b,int x,int y,int r,int c) {
    for(int dy=-r;dy<=r;++dy) {
        int dx=r; while(dx>0 && dx*dx+dy*dy>r*r) --dx;
        ht_span(b,x-dx,x+dx,y+dy,c);
    }
}
static void ht_tree(uint8_t *b,int x,int base,int height,int width,uint32_t seed,int c) {
    uint32_t h=ht_hash(seed);
    int lean=(int)(h%43)-21;
    int bend=(int)((h>>8)%19)-9;
    int kx=x+lean/3+bend, ky=base-height*52/100;
    int tx=x+lean,ty=base-height;
    ht_limb(b,x,base,kx,ky,width,ht_max(2,width*2/3),c);
    ht_limb(b,kx,ky,tx,ty,ht_max(2,width*2/3),1,c);
    ht_limb(b,x-width*3,base+4,x,base-height/6,1,width/2,c);
    ht_limb(b,x+width*2,base+5,x,base-height/7,1,width/2,c);
    for(int j=0;j<5;++j) {
        h=ht_hash(seed+(uint32_t)j*137u);
        int side=(h&1u)?1:-1;
        int rise=30+j*12+(int)((h>>8)%9);
        int at=base-height*rise/100;
        int root=x+lean*rise/100+(rise<52?bend*rise/52:bend*(100-rise)/48);
        int reach=height*(15+(int)((h>>12)%20))/100;
        int ex=root+side*reach,ey=at-height*(4+(int)((h>>16)%17))/100;
        int mx=root+(ex-root)*55/100,my=at+(ey-at)/3;
        ht_limb(b,root,at,mx,my,ht_max(1,width*(8-j)/13),ht_max(1,width/4),c);
        ht_limb(b,mx,my,ex,ey,ht_max(1,width/4),0,c);
        ht_limb(b,mx,my,mx+side*reach/3,my-height/7,ht_max(1,width/5),0,c);
        if(j<3) ht_limb(b,ex-side*reach/5,ey+3,ex+side*reach/6,ey-height/9,1,0,c);
    }
}
/* Sliding box filter: fixed O(width*height), independent of blur radius.
 * Edge extension prevents the panel border from introducing a white seam. */
/* For 1 <= n <= 19 and 0 <= sum <= 255*n this upward-rounded Q24
 * reciprocal gives EXACT floor(sum/n). The product fits uint32_t. */
static uint8_t ht_average(int sum,uint32_t reciprocal) {
    return (uint8_t)(((uint32_t)sum*reciprocal)>>24);
}
static void ht_blur_horizontal(const uint8_t *src,int radius,uint32_t reciprocal,int x0,int x1,int y0,int y1) {
    for(int y=y0;y<y1;++y) {
        const uint8_t *row=src+y*HT_W; int sum=0;
        for(int k=-radius;k<=radius;++k) sum+=row[ht_clamp(x0+k,0,HT_W-1)];
        for(int x=x0;x<x1;++x) {
            ht_filter[y*HT_W+x]=ht_average(sum,reciprocal);
            sum+=row[ht_min(HT_W-1,x+radius+1)]-row[ht_max(0,x-radius)];
        }
        if((y&31)==31) ht_checkpoint();
    }
}
static void ht_blur_rect(const uint8_t *src,uint8_t *dst,int radius,int x0,int x1,int y0,int y1) {
    if(!radius) {
        for(int y=y0;y<y1;++y) {
            if(ht_math) (void)ht_math->copy_bytes(dst+y*HT_W+x0,src+y*HT_W+x0,(size_t)(x1-x0));
            else memcpy(dst+y*HT_W+x0,src+y*HT_W+x0,(size_t)(x1-x0));
        }
        return;
    }
    int n=radius*2+1;
    uint32_t reciprocal=(0x1000000u+(uint32_t)n-1u)/(uint32_t)n;
    ht_blur_horizontal(src,radius,reciprocal,x0,x1,ht_max(0,y0-radius),ht_min(HT_H,y1+radius));
    /* Keep vertical sums in 960 bytes of stack and visit PSRAM by rows.
     * Column-strided reads would evict cache lines on every sample. */
    int16_t sums[HT_W] __attribute__((aligned(16)))={0};
    for(int k=-radius;k<=radius;++k) {
        const int16_t *row=ht_filter+ht_clamp(y0+k,0,HT_H-1)*HT_W;
        for(int x=x0;x<x1;++x) sums[x]+=row[x];
    }
    for(int y=y0;y<y1;++y) {
        const int16_t *add=ht_filter+ht_min(HT_H-1,y+radius+1)*HT_W;
        const int16_t *sub=ht_filter+ht_max(0,y-radius)*HT_W;
        uint8_t *row=dst+y*HT_W;
        for(int x=x0;x<x1;++x) {
            row[x]=ht_average(sums[x],reciprocal);
        }
        if(y+1==y1) break;
        if(ht_math) {
            /* 0..4845 sums, 0..255 samples: even the temporary addition
             * fits int16_t exactly. SIMD handles eight columns at a time. */
            (void)ht_math->add_s16(sums+x0,add+x0,sums+x0,(size_t)(x1-x0));
            (void)ht_math->sub_s16(sums+x0,sub+x0,sums+x0,(size_t)(x1-x0));
        } else for(int x=x0;x<x1;++x) sums[x]=(int16_t)(sums[x]+add[x]-sub[x]);
        if((y&15)==15) ht_checkpoint();
    }
}
static void ht_blur_region(const uint8_t *src,uint8_t *dst,int radius,int x0,int x1) {
    ht_blur_rect(src,dst,radius,x0,x1,0,HT_H);
}
static void ht_composite(int depth,int focus_x,int focus_y) {
    /* One pass: blend, fade, and max composite without PSRAM scratch rows.
     * Keep both original rounding points. All intermediates fit signed 32-bit
     * for screen-relative focus coordinates supplied by ht_render_scene. */
    const int fade=depth==2?70:45;
    int dy=HT_BORDER-focus_y,dy2=dy*dy;
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y) {
        const uint8_t *near=ht_near+y*HT_W,*wide=ht_wide+y*HT_W;
        uint8_t *scene=ht_scene+y*HT_W;
        int left=ht_visible_left[y];
        int distance=(left-focus_x)*(left-focus_x)+dy2;
        int delta=1+2*(left-focus_x);
        for(int x=left;x<HT_W-left;++x) {
            int v;
            if(distance<=2500) {
                v=near[x]; /* Fully focused: no blur mix or fade. */
            } else if(distance>=53700) {
                v=wide[x]*(256-fade)>>8; /* Fully fogged outer region. */
            } else {
                const int radial=(distance-2500)/200;
                /* a*(256-r)+b*r = a*256+(b-a)*r: one product for mixing. */
                v=(near[x]*256+((int)wide[x]-near[x])*radial)>>8;
                v=v*(256-((radial*fade)>>8))>>8;
            }
            scene[x]=(uint8_t)ht_max(scene[x],v);
            distance+=delta; delta+=2; /* (dx+1)^2 from dx^2. */
        }
        dy2+=2*dy+1; ++dy;
        if((y&31)==31) ht_checkpoint();
    }
}
/* Skyline details are generated only when a world strip enters the cache.
 * World-anchored cells and culling margins keep overlapping strips identical. */
static void ht_city_background(int depth,int offset) {
    int spacing=depth==0?59:173, tone=depth==0?85:165;
    int first=ht_floor_div(offset-130,spacing),last=ht_floor_div(offset+HT_W+130,spacing);
    for(int j=first;j<=last;++j) {
        uint32_t h=ht_hash((uint32_t)j+411u+(unsigned)depth*973u);
        int x=j*spacing-offset, w=depth==0?spacing-9-(int)(h%13):80+(int)(h%37);
        int top=(depth==0?48:53)+(int)((h>>8)%97);
        ht_rect(ht_raw,x,top,w,HT_H-top,tone);
        ht_rect(ht_raw,x+6,top-9,w-12,10,tone);
        if(h&1u) {
            ht_rect(ht_raw,x+w/2-2,top-30,3,23,tone);
            ht_rect(ht_raw,x+w/2-9,top-14,18,3,tone);
        }
        /* Sparse windows stay subdued behind the fog. */
        for(int y=top+12;y<HT_H;y+=17)
            for(int col=7;col<w-5;col+=13)
                if((ht_hash(h+(unsigned)(y*7+col))&3u)!=0)
                    ht_rect(ht_raw,x+col,y,4,7,tone-28);
        ht_checkpoint();
    }
}
static void ht_city_foreground(int camera) {
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&ht_city[i];
        int l=p->left-camera,r=p->right-camera,top=p->top;
        if(r< -20 || l>HT_W+20) continue;
        ht_rect(ht_raw,l,top,r-l,HT_H-top,255);
        ht_rect(ht_raw,l,top+5,r-l,3,205);
        ht_rect(ht_raw,l+5,top+9,5,HT_H-top,224);
        for(int wx=p->left+21;wx<p->right-13;wx+=24)
            for(int y=top+19;y<HT_H;y+=23)
                ht_rect(ht_raw,wx-camera,y,8,12,215);
        /* Equipment stays decorative: none rises into the jump path. */
        ht_rect(ht_raw,l+17,top+10,25,4,235);
        ht_checkpoint();
    }
    /* Rooftop exit beacon: open doorway and antenna above the last roof. */
    int x=HT_GOAL-camera,top=ht_city[HT_PLATFORMS-1].top;
    ht_rect(ht_raw,x-19,top-49,7,49,255);
    ht_rect(ht_raw,x+12,top-49,7,49,255);
    ht_rect(ht_raw,x-23,top-54,46,8,255);
    ht_rect(ht_raw,x-1,top-78,2,24,255);
    ht_disk(ht_raw,x,top-80,3,180);
}
static void ht_cactus_on(uint8_t *target,int x,int base,int height,int tone) {
    /* The scenery is sampled every two logical pixels. Even limb widths,
     * at least four pixels, keep the small cactus two samples thick at both
     * camera parities instead of alternating between one and two samples. */
    int w=(ht_max(4,height/10)+1)&~1;
    int reach=height/3,left=x-reach,right=x+reach-w;
    int left_top=base-height*4/5,left_join=base-height*3/5;
    int right_top=base-height*2/3,right_join=base-height*2/5;
    ht_rect(target,x-w/2,base-height,w,height,tone);
    ht_disk(target,x,base-height,w/2,tone);
    ht_rect(target,left,left_join,reach,w,tone);
    ht_rect(target,left,left_top,w,left_join-left_top+w,tone);
    ht_disk(target,left+w/2,left_top,w/2,tone);
    ht_rect(target,x,right_join,reach,w,tone);
    /* Derive the vertical end from the exact shared elbow, rather than
     * independently rounded height fractions which could leave a gap. */
    ht_rect(target,right,right_top,w,right_join-right_top+w,tone);
    ht_disk(target,right+w/2,right_top,w/2,tone);
}
/* Static machinery: construction/blur is cached, not animated per frame. */
static void ht_cactus(int x,int base,int height,int tone) {ht_cactus_on(ht_raw,x,base,height,tone);}

static void ht_derrick(int x,int base,int height,int tone) {
    int top=base-height,w=height/4;
    ht_limb(ht_raw,x-w,base,x-4,top,3,2,tone);
    ht_limb(ht_raw,x+w,base,x+4,top,3,2,tone);
    for(int k=1;k<=4;++k) {
        int y=top+height*k/5,half=4+w*k/5;
        ht_rect(ht_raw,x-half,y,half*2,3,tone);
        ht_limb(ht_raw,x-half,y,x+ht_max(3,half-w/5),y-height/5,1,1,tone);
    }
    ht_rect(ht_raw,x-8,top-5,16,5,tone);
}
static void ht_oil_background(int depth,int offset) {
    int tone=depth==0?80:158,base=depth==0?230:251;
    int spacing=depth==0?211:337;
    int first=ht_floor_div(offset-200,spacing),last=ht_floor_div(offset+HT_W+200,spacing);
    for(int j=first;j<=last;++j) {
        uint32_t h=ht_hash((uint32_t)j+902u);
        int x=j*spacing-offset,top=base-30-(int)(h%31);
        /* Broad flat-topped desert mesas. */
        ht_triangle(ht_raw,x-80,base,x-20,top,x+80,base,tone-28);
        ht_rect(ht_raw,x-20,top,75,HT_H-top,tone-28);
        ht_triangle(ht_raw,x+55,top,x+145,base,x+55,base,tone-28);
        if(depth==0 || (j&1)) ht_derrick(x+40,base,110+(int)(h%65),tone);
        else {
            /* Pumpjack pedestal, walking beam, horsehead and polished rod. */
            ht_limb(ht_raw,x+20,base,x+37,base-57,5,3,tone);
            ht_limb(ht_raw,x+54,base,x+37,base-57,5,3,tone);
            ht_limb(ht_raw,x-19,base-70,x+87,base-49,5,6,tone);
            ht_rect(ht_raw,x+79,base-52,13,22,tone);
            ht_rect(ht_raw,x+87,base-30,2,30,tone);
            ht_disk(ht_raw,x-4,base-18,13,tone);
            ht_limb(ht_raw,x-4,base-18,x+2,base-65,2,2,tone);
            ht_rect(ht_raw,x-23,base-4,122,5,tone);
        }
        if(depth==1) ht_cactus(x+146,base,45+(int)(h%25),tone);
        ht_checkpoint();
    }
}
static void ht_oil_foreground(int camera) {
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&ht_oil[i];
        int l=p->left-camera,r=p->right-camera;
        if(r< -50 || l>HT_W+50) continue;
        ht_rect(ht_raw,l,p->top,r-l,HT_H-p->top,255);
        ht_triangle(ht_raw,l,p->top+8,l-8,p->top+38,l,p->top+65,255);
        ht_triangle(ht_raw,r-1,p->top+14,r+6,p->top+43,r-1,p->top+70,255);
        ht_cactus(l+(r-l)/2,p->top,65+(i%3)*19,255);
        if(i%3==0) ht_cactus(l+65,p->top,39,255);
        for(int wx=p->left+18;wx<p->right-15;wx+=41)
            ht_rect(ht_raw,wx-camera,p->top+11,13,2,233);
        ht_checkpoint();
    }
    int x=HT_GOAL-camera,top=ht_oil[HT_PLATFORMS-1].top;
    ht_rect(ht_raw,x-19,top-49,7,49,255);
    ht_rect(ht_raw,x+12,top-49,7,49,255);
    ht_rect(ht_raw,x-23,top-54,46,8,255);
    ht_triangle(ht_raw,x-25,top-54,x,top-69,x+25,top-54,255);
}
/* Chapter scenery is world-anchored and generated only into cached strips. */
static void ht_chapter_background(int depth,int offset) {
    int spacing=211, tone=depth?162:83,base=depth?253:233;
    int first=ht_floor_div(offset-220,spacing),last=ht_floor_div(offset+HT_W+220,spacing);
    for(int j=first;j<=last;++j) {
        int x=j*spacing-offset;
        uint32_t h=ht_hash((uint32_t)j+ht_geometry_level*731u);
        int height=85+(int)(h%70),top=base-height;
        switch(ht_geometry_level) {
        case 3: /* Freight cars, signals and distant railway gantries. */
            ht_rect(ht_raw,x-30,base-50,155,40,tone);
            for(int k=0;k<6;++k) ht_rect(ht_raw,x-23+k*24,base-45,2,29,tone-22);
            ht_disk(ht_raw,x,base-8,9,tone); ht_disk(ht_raw,x+91,base-8,9,tone);
            ht_rect(ht_raw,x-44,base,220,4,tone);
            ht_rect(ht_raw,x+145,top,5,height,tone);
            ht_rect(ht_raw,x+138,top-7,19,29,tone);
            ht_disk(ht_raw,x+147,top,3,tone-50);
            if(!depth) { ht_rect(ht_raw,x-10,top-20,5,height+20,tone); ht_rect(ht_raw,x-10,top-20,165,5,tone); }
            break;
        case 4: /* Sunken ferry shelters, willows and reed beds. */
            ht_tree(ht_raw,x,base,height,5,h,tone);
            ht_rect(ht_raw,x+84,base-58,5,70,tone); ht_rect(ht_raw,x+148,base-58,5,70,tone);
            ht_triangle(ht_raw,x+73,base-58,x+116,base-89,x+164,base-58,tone);
            for(int k=0;k<9;++k) ht_limb(ht_raw,x+70+k*11,base+10,x+67+k*11,base-8-(k%3)*9,1,1,tone);
            ht_rect(ht_raw,x-40,base+15,210,2,tone-35);
            break;
        case 5: /* Benched quarry walls and abandoned lifting cranes. */
            for(int k=0;k<4;++k) ht_rect(ht_raw,x-20+k*19,top+k*25,160-k*12,HT_H,tone-18+k*4);
            ht_rect(ht_raw,x+130,top-20,5,height+20,tone);
            ht_limb(ht_raw,x+130,top-18,x+44,top+4,3,2,tone);
            ht_rect(ht_raw,x+45,top+4,2,48,tone);
            break;
        case 6: /* Empty glasshouse ribs, climbing vines and seed beds. */
            ht_rect(ht_raw,x-20,base-69,161,3,tone);
            for(int k=0;k<5;++k) {
                int bx=x-20+k*40;
                ht_rect(ht_raw,bx,base-69,3,69,tone);
                ht_limb(ht_raw,bx,base-68,x+60,base-119,2,2,tone);
            }
            ht_limb(ht_raw,x+60,base-119,x+141,base-68,3,3,tone);
            ht_tree(ht_raw,x+164,base,height,4,h,tone);
            ht_rect(ht_raw,x-20,base-4,166,4,tone);
            break;
        case 7: /* Dam buttresses, spillways and turbine pipework. */
            ht_rect(ht_raw,x-15,top,185,HT_H-top,tone);
            ht_rect(ht_raw,x-23,top,201,8,tone);
            for(int k=0;k<4;++k) {
                ht_rect(ht_raw,x+k*44,top+20,21,height,tone-48);
                ht_rect(ht_raw,x+k*44+8,top+29,4,height,tone-20);
            }
            ht_rect(ht_raw,x-15,top-14,185,3,tone);
            for(int k=0;k<7;++k) ht_rect(ht_raw,x-15+k*30,top-14,2,14,tone);
            break;
        case 8: /* Jagged mountain ridges and wind-stripped pines. */
            ht_triangle(ht_raw,x-100,HT_H,x+20,top-30,x+177,HT_H,tone-15);
            ht_triangle(ht_raw,x+20,top-30,x+4,top+8,x+60,top+28,tone-42);
            if(depth) ht_tree(ht_raw,x+146,base,height,5,h,tone);
            break;
        default: /* Settlement lights under the radio mast. */
            ht_rect(ht_raw,x-10,base-57,101,58,tone);
            ht_triangle(ht_raw,x-24,base-57,x+37,base-100,x+108,base-57,tone);
            ht_rect(ht_raw,x+63,base-96,10,44,tone);
            for(int k=0;k<3;++k) ht_rect(ht_raw,x+k*29,base-39,12,19,0);
            ht_derrick(x+153,base,height+35,tone);
            ht_limb(ht_raw,x+128,top,x+178,top,2,2,tone);
            break;
        }
        ht_checkpoint();
    }
}
static void ht_chapter_foreground(int camera) {
    const ht_platform *land=ht_level_land(ht_geometry_level);
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&land[i];
        int l=p->left-camera,r=p->right-camera,top=p->top;
        if(r< -80 || l>HT_W+80) continue;
        ht_rect(ht_raw,l,top,r-l,HT_H-top,255);
        if(ht_geometry_level==4) { /* Broken boardwalks above dark water. */
            for(int x=l+8;x<r-8;x+=24) ht_rect(ht_raw,x,top+5,3,HT_H-top,65);
            ht_rect(ht_raw,l,top+8,r-l,5,255);
            for(int k=0;k<4;++k) ht_limb(ht_raw,l+50+k*8,top,l+46+k*8,top-15-(k%2)*12,1,1,255);
        } else if(ht_geometry_level==3 || ht_geometry_level==7) {
            ht_rect(ht_raw,l,top+7,r-l,3,212);
            for(int x=l+16;x<r-12;x+=42) ht_rect(ht_raw,x,top+18,16,26,227);
        } else {
            ht_triangle(ht_raw,l,top+5,l-7,top+34,l,top+61,255);
            ht_triangle(ht_raw,r-1,top+15,r+7,top+45,r-1,top+70,255);
            if(ht_geometry_level==6 || ht_geometry_level==8)
                ht_tree(ht_raw,l+(r-l)*2/3,top,ht_geometry_level==6?87:123,5,(uint32_t)i+140u,255);
            else if(ht_geometry_level==9) {
                ht_rect(ht_raw,l+56,top-25,3,25,255);
                ht_rect(ht_raw,l+52,top-32,11,10,255);
                ht_rect(ht_raw,l+55,top-30,5,6,0);
            }
        }
        ht_checkpoint();
    }
    int x=HT_GOAL-camera,top=land[HT_PLATFORMS-1].top;
    ht_rect(ht_raw,x-19,top-49,7,49,255); ht_rect(ht_raw,x+12,top-49,7,49,255);
    ht_rect(ht_raw,x-23,top-54,46,8,255);
    if(ht_geometry_level==9) { /* A waiting silhouette beside the home doorway. */
        ht_disk(ht_raw,x+37,top-25,4,255);
        ht_rect(ht_raw,x+34,top-21,7,14,255);
        ht_limb(ht_raw,x+36,top-8,x+33,top,2,1,255);
        ht_limb(ht_raw,x+39,top-8,x+43,top,2,1,255);
    }
}
static void ht_background(int depth,int offset) {
    if(ht_geometry_level>=3) { ht_chapter_background(depth,offset); return; }
    if(ht_geometry_level==2) { ht_oil_background(depth,offset); return; }
    if(ht_geometry_level==1) { ht_city_background(depth,offset); return; }
    int tone=depth==0?85:170, base=depth==0?219:246;
    /* Trace the same hill boundary, then fill contiguous rows. Drawing
     * one-pixel-wide vertical rectangles thrashed PSRAM/cache and called
     * memset tens of thousands of times per frame. ht_temp is scratch here. */
    int lowest=HT_H;
    for(int x=0;x<HT_W;++x) {
        int wx=x+offset, cell=ht_floor_div(wx,220), t=wx-cell*220;
        int a=base-20-(int)(ht_hash((uint32_t)cell+70u+(unsigned)depth)%35);
        int b=base-20-(int)(ht_hash((uint32_t)cell+71u+(unsigned)depth)%35);
        int top=a+(b-a)*t/220-(t*(220-t))/1700;
        ht_temp[x]=(uint8_t)top;
        lowest=ht_min(lowest,top);
    }
    for(int y=lowest;y<HT_H;++y) {
        uint8_t *row=ht_raw+y*HT_W;
        for(int x=0;x<HT_W;++x) if(y>=ht_temp[x]) row[x]=(uint8_t)(tone-15);
        if((y&31)==31) ht_checkpoint();
    }
    int spacing=depth==0?103:169;
    int start=ht_floor_div(offset-160,spacing),end=ht_floor_div(offset+HT_W+160,spacing);
    for(int j=start;j<=end;++j) {
        uint32_t h=ht_hash((uint32_t)(j+80+depth*59));
        int x=j*spacing-offset+(int)(h%31),height=(depth==0?101:208)+(int)(h%90);
        ht_tree(ht_raw,x,base,height,depth==0?3:10,h,tone);
        ht_checkpoint();
    }
    /* Abandoned pitched-roof mill and a distant watchtower. */
    int period=depth==0?680:1040;
    for(int j=0;j<6;++j) {
        int x=330+j*period-offset;
        if(x< -160 || x>HT_W+160) continue;
        int y=base-64;
        ht_rect(ht_raw,x,y,64,85,tone);
        ht_triangle(ht_raw,x-8,y,x+30,y-30,x+73,y,tone);
        ht_rect(ht_raw,x+49,y-34,8,27,tone);
        ht_rect(ht_raw,x+11,y+17,9,16,0);
        ht_rect(ht_raw,x+39,y+17,9,16,0);
        if(depth==0) {
            ht_rect(ht_raw,x+86,y-45,19,111,tone);
            ht_triangle(ht_raw,x+80,y-45,x+95,y-64,x+111,y-45,tone);
            ht_rect(ht_raw,x+92,y-30,6,17,0);
        }
        ht_checkpoint();
    }
}
static void ht_foreground(int camera) {
    if(ht_geometry_level>=3) { ht_chapter_foreground(camera); return; }
    if(ht_geometry_level==2) { ht_oil_foreground(camera); return; }
    if(ht_geometry_level==1) { ht_city_foreground(camera); return; }
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&ht_forest[i];
        int l=p->left-camera,r=p->right-camera;
        if(r< -256 || l>=HT_W+256) continue;
        ht_rect(ht_raw,l,p->top,r-l,HT_H-p->top,255);
        /* Fine roots and grass leave the collision edge clearly legible. */
        for(int wx=p->left+8;wx<p->right-8;wx+=11) {
            uint32_t h=ht_hash((uint32_t)wx);
            int x=wx-camera;
            ht_limb(ht_raw,x,p->top,x+(int)(h%7)-3,p->top-2-(int)(h%7),1,0,255);
        }
        ht_triangle(ht_raw,l,p->top+10,l-6,p->top+31,l,p->top+70,255);
        ht_triangle(ht_raw,r-1,p->top+19,r+7,p->top+49,r-1,p->top+80,255);
        /* Trees stay on firm ground, away from takeoff/landing edges. */
        int tx=p->left+(p->right-p->left)*2/3;
        if(i!=9) ht_tree(ht_raw,tx-camera,p->top,225+(i%3)*20,12+(i%3)*3,(uint32_t)i+901,255);
        if(i==0) ht_tree(ht_raw,15-camera,p->top,270,22,146,255);
        if(i==9) ht_tree(ht_raw,2950-camera,p->top,265,18,766,255);
        ht_checkpoint();
    }
    /* Goal: a ruined doorway with an open white passage. */
    int x=HT_GOAL-camera;
    ht_rect(ht_raw,x-19,151,7,49,255); ht_rect(ht_raw,x+12,151,7,49,255);
    ht_rect(ht_raw,x-23,146,46,8,255);
}
/* This plane has its own 1.23x translation and cached blur. Enumerate
 * periodic branches by world position so adjacent strips agree exactly. */
static void ht_branches(int offset) {
    if(ht_geometry_level>=3) {
        int first=ht_floor_div(offset-200,811),last=ht_floor_div(offset+HT_W+200,811);
        for(int j=first;j<=last;++j) {
            int x=j*811-offset;
            if(ht_geometry_level==4 || ht_geometry_level==6 || ht_geometry_level==8) {
                ht_limb(ht_raw,x-75,-10,x+73,34,12,3,255);
                ht_limb(ht_raw,x+15,17,x+64,72,4,0,255);
            } else {
                ht_rect(ht_raw,x-70,20,150,7,255);
                ht_limb(ht_raw,x-58,22,x+19,65,4,3,255);
                ht_rect(ht_raw,x+14,0,8,68,255);
            }
        }
        return;
    }
    if(ht_geometry_level==2) {
        int first=ht_floor_div(offset-200,830),last=ht_floor_div(offset+HT_W+200,830);
        for(int j=first;j<=last;++j) {
            int x=j*830-offset;
            /* Overhead oil-field service gantries frame only the top plane. */
            ht_rect(ht_raw,x,0,10,81,255);
            ht_rect(ht_raw,x-54,25,118,7,255);
            ht_limb(ht_raw,x-48,30,x+5,68,3,3,255);
            ht_limb(ht_raw,x+58,30,x+5,68,3,3,255);
        }
        return;
    }
    if(ht_geometry_level==1) {
        int first=ht_floor_div(offset-300,720),last=ht_floor_div(offset+HT_W+300,720);
        for(int j=first;j<=last;++j) {
            int x=j*720-offset;
            /* Close overhead crane beams and hanging cables, confined to
             * the same top 90 rows as the forest foreground plane. */
            ht_rect(ht_raw,x-90,19,280,8,255);
            ht_limb(ht_raw,x-70,20,x+20,53,4,3,255);
            ht_rect(ht_raw,x+20,0,9,65,255);
            ht_rect(ht_raw,x+170,24,2,49,255);
            ht_limb(ht_raw,x+170,72,x+178,78,2,2,255);
        }
        return;
    }
    int first=ht_floor_div(offset-300,910),last=ht_floor_div(offset+HT_W+300,910);
    for(int j=first;j<=last;++j) {
        int x=75+j*910-offset;
        ht_limb(ht_raw,x-110,-20,x+100,24,18,6,255);
        ht_limb(ht_raw,x+55,15,x+140,83,5,0,255);
        ht_limb(ht_raw,x+7,0,x-34,67,7,0,255);
    }
}
static void ht_select_level(unsigned level) {
    if(ht_geometry_level==level) return;
    ht_geometry_level=level;
    ht_cache_job.active=false;
    memset(ht_cache_valid,0,sizeof(ht_cache_valid));
}
static int ht_layer_offset(int depth,int camera) {
    return depth==0?camera*22/100:depth==1?camera*53/100:depth==2?camera:camera*123/100;
}
static int ht_cache_slot(int key) { return (int)((unsigned)key&(HT_TILE_SLOTS-1u)); }
static bool ht_cache_has(int depth,int key) {
    int slot=ht_cache_slot(key);
    return ht_cache_valid[depth][slot] && ht_cache_key[depth][slot]==key;
}
static void ht_cache_begin(int depth,int key) {
    ht_cache_job.active=true; ht_cache_job.depth=depth; ht_cache_job.key=key;
    ht_cache_job.phase=0; ht_cache_job.row=0;
}
/* One geometry pass or at most 64 filter/copy rows per call, with the same
 * elapsed-time input/yield checkpoints as rendering. Publish only complete
 * strips; a direction reversal may discard an unfinished speculative job. */
static void ht_cache_work(void) {
    if(!ht_cache_job.active || ht_abort) return;
    int depth=ht_cache_job.depth,key=ht_cache_job.key;
    static const int radius[4]={3,1,0,0},spread[4]={9,6,4,4};
    if(ht_cache_job.phase==0) {
        ht_clear_layer(ht_raw);
        int offset=key*HT_TILE_W-HT_TILE_HALO;
        if(depth<2) ht_background(depth,offset);
        else if(depth==2) ht_foreground(offset);
        else ht_branches(offset);
        ht_cache_job.phase=1;
    } else {
        int y0=ht_cache_job.row,y1=ht_min(HT_H,y0+HT_CACHE_BAND);
        if(ht_cache_job.phase==1)
            ht_blur_rect(ht_raw,ht_near,radius[depth],HT_TILE_HALO-spread[depth],
                         HT_TILE_HALO+HT_TILE_W+spread[depth],y0,y1);
        else if(ht_cache_job.phase==2)
            ht_blur_rect(ht_near,ht_wide,spread[depth],HT_TILE_HALO,HT_TILE_HALO+HT_TILE_W,y0,y1);
        else {
            int slot=ht_cache_slot(key);
            /* The scheduler never evicts a strip visible in its snapshot. */
            ht_cache_valid[depth][slot]=false;
            for(int y=y0;y<y1;++y) {
                memcpy(ht_cached_near[depth][slot]+y*HT_TILE_W,ht_near+y*HT_W+HT_TILE_HALO,HT_TILE_W);
                memcpy(ht_cached_wide[depth][slot]+y*HT_TILE_W,ht_wide+y*HT_W+HT_TILE_HALO,HT_TILE_W);
                if((y&15)==15) ht_checkpoint();
            }
        }
        ht_cache_job.row=y1;
        if(y1==HT_H) {
            ht_cache_job.row=0;
            if(++ht_cache_job.phase==4) {
                int slot=ht_cache_slot(key);
                ht_cache_key[depth][slot]=key; ht_cache_valid[depth][slot]=true;
                ht_cache_job.active=false; ++ht_cache_builds;
            }
        }
    }
    ht_checkpoint();
}
static bool ht_cache_ensure(int depth,int key) {
    if(ht_cache_has(depth,key)) return true;
    if(!ht_cache_job.active || ht_cache_job.depth!=depth || ht_cache_job.key!=key)
        ht_cache_begin(depth,key);
    while(!ht_cache_has(depth,key) && !ht_abort) ht_cache_work();
    return !ht_abort;
}
static bool ht_cache_visible(int camera) {
    if(ht_abort) return false;
    for(int depth=0;depth<HT_LAYERS;++depth) {
        if(depth==2) continue; // Dynamic traversal now owns this plane.
        int offset=ht_layer_offset(depth,camera);
        int first=ht_floor_div(offset+2*(HT_BORDER/2),HT_TILE_W);
        int last=ht_floor_div(offset+HT_W-2*(HT_BORDER/2)-1,HT_TILE_W);
        for(int key=first;key<=last;++key) if(!ht_cache_ensure(depth,key)) return false;
    }
    return true;
}
/* Warm visible strips first, then one lookahead strip per plane. Called once
 * per host loop: no heap allocation and no unbounded full-level warmup. */
static bool ht_cache_prefetch(int camera,int direction) {
    if(ht_abort) return false;
    int chosen_depth=-1,chosen_key=0,nearest=1000000;
    static const int speed[HT_LAYERS]={22,53,100,123};
    for(int depth=0;depth<HT_LAYERS;++depth) {
        if(depth==2) continue; // Dynamic traversal now owns this plane.
        int offset=ht_layer_offset(depth,camera);
        int first=ht_floor_div(offset+2*(HT_BORDER/2),HT_TILE_W);
        int last=ht_floor_div(offset+HT_W-2*(HT_BORDER/2)-1,HT_TILE_W);
        for(int key=first;key<=last;++key) if(!ht_cache_has(depth,key)) {
            chosen_depth=depth; chosen_key=key; nearest=-1; break;
        }
        if(nearest<0) break;
        int key=direction<0?first-1:last+1;
        if(!ht_cache_has(depth,key)) {
            int gap=direction<0?offset+2*(HT_BORDER/2)-first*HT_TILE_W:
                (last+1)*HT_TILE_W-(offset+HT_W-2*(HT_BORDER/2)-1);
            /* Prepare the strip needed soonest in camera-space, rather than
             * always serving the slowest background layer first. */
            int distance=gap*100/speed[depth];
            if(distance<nearest) { nearest=distance; chosen_depth=depth; chosen_key=key; }
        }
    }
    if(chosen_depth<0) { ht_cache_job.active=false; return false; }
    if(!ht_cache_job.active || ht_cache_job.depth!=chosen_depth || ht_cache_job.key!=chosen_key)
        ht_cache_begin(chosen_depth,chosen_key);
    ht_cache_work();
    return true;
}
/* Evaluate lighting at one sample per 2x2 world pixels. Cached source art
 * retains its original detail and integer parallax offsets, including odd
 * offsets at strip boundaries. No physics or camera coordinates are scaled. */
/* A/B experiment: only the cached per-frame compositor changes. Cache filters,
 * output dithering and timing checkpoints stay the same. Defaults to scalar. */
static bool ht_dsp_composite;
static bool ht_dsp_available(void) {
    return ht_math && ht_math->struct_size>=offsetof(t5_math_api_v1,mul_s16)+sizeof(ht_math->mul_s16) &&
           (ht_math->features&T5_MATH_FEATURE_S3_DSP) && ht_math->mul_s16;
}
#define HT_DSP_BATCH (HT_TILE_W/2)
/* Fixed, aligned app-owned scratch; no allocation or large task-stack frame.
 * Each tile span has at most 128 samples. The bridge handles vector tails. */
static int16_t ht_dsp_a[HT_DSP_BATCH] __attribute__((aligned(16)));
static int16_t ht_dsp_b[HT_DSP_BATCH] __attribute__((aligned(16)));
static int16_t ht_dsp_product[HT_DSP_BATCH] __attribute__((aligned(16)));
static int16_t ht_dsp_radial[HT_DSP_BATCH] __attribute__((aligned(16)));
static void ht_dsp_multiply(int count) {
    if(ht_math->mul_s16(ht_dsp_a,ht_dsp_b,ht_dsp_product,(size_t)count)) return;
    for(int i=0;i<count;++i) ht_dsp_product[i]=(int16_t)(ht_dsp_a[i]*ht_dsp_b[i]);
}
static void ht_composite_dsp_span(uint8_t *dst,const uint8_t *near,const uint8_t *wide,
                                  int count,int distance,int delta,int fade) {
    for(int i=0;i<count;++i) {
        int radial=ht_clamp((distance-2500)/200,0,256);
        ht_dsp_radial[i]=(int16_t)radial;
        ht_dsp_a[i]=(int16_t)((int)wide[2*i]-near[2*i]);
        ht_dsp_b[i]=(int16_t)(radial&127);
        distance+=delta;delta+=8;
    }
    /* Signed products must fit s16. Split weights into low seven bits and
     * a 0/128/256 correction, accumulated in 32 bits. Max DSP product 32385.
     * This retains the scalar rounding, including negative blur differences. */
    ht_dsp_multiply(count);
    for(int i=0;i<count;++i) {
        int radial=ht_dsp_radial[i];
        int v=(near[2*i]*256+ht_dsp_product[i]+ht_dsp_a[i]*(radial&~127))>>8;
        int factor=256-((radial*fade)>>8);
        ht_dsp_a[i]=(int16_t)v; ht_dsp_b[i]=(int16_t)(factor&127);
        ht_dsp_radial[i]=(int16_t)(factor&~127);
    }
    ht_dsp_multiply(count);
    for(int i=0;i<count;++i) {
        int v=(ht_dsp_product[i]+ht_dsp_a[i]*ht_dsp_radial[i])>>8;
        dst[i]=(uint8_t)ht_max(dst[i],v);
    }
}
static __attribute__((optimize("O3"))) void ht_composite_cached(int depth,int offset,int focus_x,int focus_y) {
    const bool use_dsp=ht_dsp_composite && ht_dsp_available();
    const uint16_t *weights=use_dsp?NULL:ht_focus_weights(focus_x,focus_y);
    const int fade=depth>=2?70:45;
    const int height=depth==3?45:HT_SCENE_H-HT_BORDER/2;
    /* The half-resolution grid begins at logical 8 for a 9px border.
     * Lighting must use the sample coordinate, not the odd border value. */
    const int start=2*(HT_BORDER/2);
    int dy=start-focus_y,dy2=dy*dy;
    for(int y=HT_BORDER/2;y<height;++y) {
        uint8_t *scene=ht_low_scene+y*HT_SCENE_W;
        for(int x=HT_BORDER/2;x<HT_SCENE_W-HT_BORDER/2;) {
            int world=offset+2*x,key=ht_floor_div(world,HT_TILE_W),column=world-key*HT_TILE_W;
            int count=ht_min(HT_SCENE_W-HT_BORDER/2-x,(HT_TILE_W-column+1)/2),slot=ht_cache_slot(key);
            const uint8_t *near=ht_cached_near[depth][slot]+(2*y)*HT_TILE_W+column;
            const uint8_t *wide=ht_cached_wide[depth][slot]+(2*y)*HT_TILE_W+column;
            if(use_dsp) {
                int dx=2*x-focus_x,distance=dx*dx+dy2,delta=4+4*dx;
                ht_composite_dsp_span(scene+x,near,wide,count,distance,delta,fade);
                x+=count; continue;
            }
            for(int i=0;i<count;++i,++x) {
                int v;
                int radial=weights[y*HT_SCENE_W+x];
                if(radial==0) v=near[2*i];
                else if(radial==256) v=wide[2*i]*(256-fade)>>8;
                else {
                    v=(near[2*i]*256+((int)wide[2*i]-near[2*i])*radial)>>8;
                    v=v*(256-((radial*fade)>>8))>>8;
                }
                scene[x]=(uint8_t)ht_max(scene[x],v);
            }
        }
        dy2+=4*dy+4; dy+=2;
        if((y&15)==15) ht_checkpoint();
    }
}
/* Expand once to the existing overlay surface, then draw the character/UI.
 * Final packing supplies the second 2x interpolation and physical-pixel dots.
 * Two-stage bilinear interpolation intentionally softens the scenery only. */
/* Four independent byte lanes in one 32-bit register. The xor mask prevents
 * cross-byte carry while retaining floor((a+b)/2), including odd inputs. */
static inline __attribute__((always_inline)) uint32_t ht_average4(uint32_t a,uint32_t b) {
    return (a&b)+(((a^b)&0xfefefefeu)>>1);
}
static inline __attribute__((always_inline)) uint32_t ht_load4(const uint8_t *p) {
    /* ht_bind receives aligned storage; row stride and block x are /4. */
    p=(const uint8_t *)__builtin_assume_aligned(p,4);
    uint32_t value; memcpy(&value,p,sizeof(value));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    value=__builtin_bswap32(value);
#endif
    return value;
}
static inline __attribute__((always_inline)) void ht_store4(uint8_t *p,uint32_t value) {
    p=(uint8_t *)__builtin_assume_aligned(p,4);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    value=__builtin_bswap32(value);
#endif
    memcpy(p,&value,sizeof(value));
}
/* Interleave the low two original/interpolated byte lanes into four pixels. */
static inline __attribute__((always_inline)) uint32_t ht_zip2(uint32_t a,uint32_t b) {
    return (a&255u)|((b&255u)<<8)|((a&0xff00u)<<8)|((b&0xff00u)<<16);
}
static __attribute__((optimize("O3"))) void ht_upscale_scene(void) {
    for(int y=0;y<HT_SCENE_H;++y) {
        const uint8_t *row=ht_low_scene+y*HT_SCENE_W;
        const uint8_t *next=ht_low_scene+ht_min(y+1,HT_SCENE_H-1)*HT_SCENE_W;
        uint8_t *top=ht_scene+2*y*HT_W,*bottom=top+HT_W;
        for(int x=0;x<HT_SCENE_W;x+=4) {
            uint32_t a=ht_load4(row+x),c=ht_load4(next+x);
            int nx=ht_min(x+4,HT_SCENE_W-1);
            uint32_t b=(a>>8)|((uint32_t)row[nx]<<24);
            uint32_t d=(c>>8)|((uint32_t)next[nx]<<24);
            uint32_t h=ht_average4(a,b),v=ht_average4(a,c);
            uint32_t diagonal=ht_average4(h,ht_average4(c,d));
            ht_store4(top+2*x,ht_zip2(a,h));
            ht_store4(top+2*x+4,ht_zip2(a>>16,h>>16));
            ht_store4(bottom+2*x,ht_zip2(v,diagonal));
            ht_store4(bottom+2*x+4,ht_zip2(v>>16,diagonal>>16));
        }
        if((y&15)==15) ht_checkpoint();
    }
}
static void ht_character(int x,int feet,const ht_game *game) {
    /* Local mist separation keeps the player visible when a black trunk
     * crosses behind them. Never wash out the landing surface underfoot. */
    for(int y=ht_max(0,feet-43);y<ht_min(HT_H,feet);++y) {
        for(int px=ht_max(0,x-25);px<=ht_min(HT_W-1,x+25);++px) {
            int dx=px-x,dy=y-(feet-19),d=dx*dx+dy*dy;
            if(d<625) {
                int ceiling=170+85*ht_clamp(d-100,0,525)/525;
                int i=y*HT_W+px;
                if(ht_scene[i]>ceiling) ht_scene[i]=(uint8_t)ceiling;
            }
        }
    }
    int swing=game->grounded?((game->stride/700)%12-6):3;
    int f=game->facing;
    ht_disk(ht_scene,x,feet-23,5,255);
    ht_limb(ht_scene,x,feet-18,x-f,feet-7,3,3,255);
    ht_limb(ht_scene,x-f,feet-8,x+swing,feet,2,1,255);
    ht_limb(ht_scene,x-f,feet-8,x-swing,feet-1,2,1,255);
    if(game->traversal.mode==HT_LEDGE || game->traversal.mode==HT_CLIMB) {
        ht_limb(ht_scene,x,feet-17,x+f*6,feet-28,2,1,255);
        ht_limb(ht_scene,x-2,feet-17,x+f*4,feet-28,1,1,255);
    } else if(game->traversal.mode==HT_ROPE) {
        ht_limb(ht_scene,x-2,feet-17,x,feet-24,1,1,255);
        ht_limb(ht_scene,x+2,feet-17,x,feet-24,1,1,255);
    } else if(game->traversal.mode==HT_ROLL || game->traversal.mode==HT_CRATE) {
        int object=game->traversal.mode==HT_ROLL?game->traversal.ball_x:game->traversal.crate_x;
        int reach=object>=game->x?7:-7;
        ht_limb(ht_scene,x,feet-17,x+reach,feet-15,1,1,255);
        ht_limb(ht_scene,x,feet-14,x+reach,feet-13,1,1,255);
    } else ht_limb(ht_scene,x,feet-17,x+f*5,feet-9,1,1,255);
    ht_rect(ht_scene,x+(f>0?1:-4),feet-24,2,2,0);
    ht_rect(ht_scene,x+(f>0?4:-1),feet-24,1,2,0);
}
/* Darkness alpha is screen-fixed; it never changes the world or camera.
 * Apply once after all layers, with no multiply in the unshaded center. */
static void ht_vignette(void) {
    for(int y=0;y<HT_H;++y) {
        uint8_t *row=ht_scene+y*HT_W;
        int left=ht_visible_left[y],clear=ht_clear_left[y];
        memset(row,255,(size_t)left); memset(row+HT_W-left,255,(size_t)left);
        /* Symmetric curved fade; the untouched central span costs no work. */
        for(int x=left;x<clear;++x) {
            int alpha=ht_mask_alpha(x,y),right=HT_W-1-x;
            row[x]=(uint8_t)(255-((255-row[x])*alpha+127)/255);
            row[right]=(uint8_t)(255-((255-row[right])*alpha+127)/255);
        }
        if((y&31)==31) ht_checkpoint();
    }
    ht_framed=true;
}
/* Render-only affine camera. Fixed-point bilinear sampling avoids pixel hops;
 * world coordinates, collision and input bearings are never transformed.
 * Phase advances only with deliberate movement and freezes at rest. */
static int ht_sway_wave(unsigned phase) {
    int t=(int)(phase&1023u);
    int triangle=t<256?t:t<768?512-t:t-1024;
    return triangle*(512-ht_abs(triangle))/256;
}
/* Raster only the visible interior. At <=42/4096 rotation and inward
 * scaling, the nine-pixel border guarantees all four source taps are valid.
 * The vignette replaces everything outside this range immediately afterward. */
static __attribute__((optimize("O3"))) void ht_sway_into(const uint8_t *source,unsigned phase) {
    int turn=ht_sway_wave(phase)/6;
    int scale=4096-(ht_sway_wave(phase/2+768)+256)/10;
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y) {
        int left=ht_visible_left[y],right=HT_W-left;
        int u=(HT_W/2)*4096+(left-HT_W/2)*scale+(y-HT_H/2)*turn;
        int v=(HT_H/2)*4096-(left-HT_W/2)*turn+(y-HT_H/2)*scale;
        uint8_t *dst=ht_scene+y*HT_W;
        for(int x=left;x<right;++x,u+=scale,v-=turn) {
            int ix=u>>12,iy=v>>12,fx=(u>>8)&15,fy=(v>>8)&15;
            const uint8_t *a=source+iy*HT_W+ix,*b=a+HT_W;
            int top=(a[0]<<4)+((int)a[1]-a[0])*fx;
            int bottom=(b[0]<<4)+((int)b[1]-b[0])*fx;
            dst[x]=(uint8_t)(((top<<4)+(bottom-top)*fy)>>8);
        }
        if((y&15)==15) ht_checkpoint();
    }
}
static void ht_sway_scene(unsigned phase) {
    if(!phase) return;
    memcpy(ht_temp,ht_scene,HT_PIXELS);
    ht_sway_into(ht_temp,phase);
}
/* Fixed particle budget, no allocations, frame noise or full-screen pass.
 * Weather shares the frozen world snapshot and pauses with simulation. */
static void ht_weather(const ht_game *g) {
    int wind=ht_wind(g),drift=g->weather_drift/256;
    for(unsigned i=0;i<48;++i) {
        uint32_t seed=ht_hash(i+g->level*97u);
        int x=(int)(seed%HT_W)+drift-g->camera/512;
        x=((x%HT_W)+HT_W)%HT_W;
        int y=(int)((seed/HT_W+(g->ticks%HT_H)*(i%2+3))%HT_H);
        ht_limb(ht_scene,x,y,x+wind/12,y+3+(int)(i%3),0,0,110+(int)(i%2)*45);
        if((i&15)==15) ht_checkpoint();
    }
    for(unsigned i=0;i<12;++i) {
        uint32_t seed=ht_hash(i+1234u+g->level*37u);
        int flutter=(int)((g->ticks+i*11)%32);flutter=flutter<16?flutter:32-flutter;
        int x=(int)(seed%HT_W)+drift*4-g->camera/256+flutter/4;
        x=((x%HT_W)+HT_W)%HT_W;
        int y=(int)((seed/HT_W+(g->ticks/3)%HT_H)%HT_H)+flutter/5;
        int lean=wind<0?-1:1;
        ht_limb(ht_scene,x,y,x+lean*3,y+(flutter<8?1:-1),1,0,180+(int)(i%2)*55);
    }
    ht_checkpoint();
}
static void ht_draw_traversal(const ht_game *game);
static void ht_draw_puzzle(const ht_game *game);
static void ht_draw_evidence(const ht_game *game);
static void ht_render_scene(void) {
    /* Input checkpoints may advance live physics. Freeze the rendered frame
     * so trees, focus and character all use one consistent game snapshot. */
    const ht_game game=ht;
    ht_select_level(game.level);
    int camera=game.camera/256,px=game.x/256-camera,py=(game.y-game.camera_y)/256-14;
    if(!ht_cache_visible(camera)) return;
    memset(ht_low_scene,0,HT_SCENE_PIXELS);
    for(int depth=0;depth<HT_LAYERS;++depth) if(depth!=2)
        ht_composite_cached(depth,ht_layer_offset(depth,camera),px,py);
    /* Render directly to the transform source: remove the former full-frame
     * copy into scratch. Input servicing never reads this raster pointer. */
    uint8_t *output=ht_scene;
    if(game.sway_phase) ht_scene=ht_temp;
    ht_upscale_scene();
    ht_draw_traversal(&game);
    ht_draw_evidence(&game);
    ht_draw_puzzle(&game);
    ht_character(px,(game.y-game.camera_y)/256,&game);
    ht_weather(&game);
    ht_scene=output;
    if(game.sway_phase) ht_sway_into(ht_temp,game.sway_phase);
    ht_vignette();
}
/* Four physical gray levels. A fixed dispersed 8x8 threshold resolves only
 * fractional blur/fog values between them, with no temporal shimmer. Exact
 * white/light/dark/black fills remain solid. Work at physical pixel pitch so
 * the blur does not turn into coarse blocks when upscaled. */
static uint8_t ht_quantize(int value,int rank) {
    /* Exact equivalent of the original q/remainder threshold; just a tiny
     * 256-byte table and bit operations in the physical-pixel hot loop. */
    static const uint8_t fraction[256]={
    0,1,2,2,3,4,5,5,6,7,8,8,9,10,11,11,
    12,13,14,14,15,16,17,17,18,19,20,20,21,22,23,23,
    24,25,26,26,27,28,29,29,30,31,32,32,33,34,35,35,
    36,37,38,38,39,40,41,41,42,43,44,44,45,46,47,47,
    48,49,50,50,51,52,53,53,54,55,56,56,57,58,59,59,
    60,61,62,62,63,64,65,66,66,67,68,69,69,70,71,72,
    72,73,74,75,75,76,77,78,78,79,80,81,81,82,83,84,
    84,85,86,87,87,88,89,90,90,91,92,93,93,94,95,96,
    96,97,98,99,99,100,101,102,102,103,104,105,105,106,107,108,
    108,109,110,111,111,112,113,114,114,115,116,117,117,118,119,120,
    120,121,122,123,123,124,125,126,126,127,128,129,130,130,131,132,
    133,133,134,135,136,136,137,138,139,139,140,141,142,142,143,144,
    145,145,146,147,148,148,149,150,151,151,152,153,154,154,155,156,
    157,157,158,159,160,160,161,162,163,163,164,165,166,166,167,168,
    169,169,170,171,172,172,173,174,175,175,176,177,178,178,179,180,
    181,181,182,183,184,184,185,186,187,187,188,189,190,190,191,192
    };
    unsigned units=fraction[value];
    return (uint8_t)((units>>6)+((unsigned)rank<(units&63u)));
}
/* Mono is the common path. Assemble whole bytes in registers; never read
 * the PSRAM destination to OR a second nibble into it. Reuse adjacent source
 * samples and keep the format branch outside the physical-pixel loop. */
/* Gather lane sign bits into bits 7,5,3,1 without a lookup or byte loop. */
static inline __attribute__((always_inline)) uint8_t ht_spread4(uint32_t bits) {
    return (uint8_t)(((bits>>7)*0x80200802u)>>24);
}
/* Bayer's four threshold quadrants each span only 64 values. All subtraction
 * lanes start >=128 and subtract <=127: borrowing cannot cross a byte. The
 * source MSB selects the unconditional result outside that threshold half. */
static inline __attribute__((always_inline)) uint8_t ht_dots4(uint32_t value,uint32_t threshold,bool upper) {
    uint32_t delta=(value|0x80808080u)-threshold;
    uint32_t bits=(upper?(delta&value):(delta|value))&0x80808080u;
    return ht_spread4(bits);
}
/* A constant 2x2 source neighborhood needs only one precomputed dither pair.
 * 2 KiB of app-owned tables; physical Bayer phase remains exactly unchanged. */
static uint16_t ht_solid_dots[4][256];
static void ht_pack_tables_init(void) {
    static const uint32_t thresholds[4]={0x3f0f3303u,0x1f2f1323u,0x37073b0bu,0x17271b2bu};
    for(unsigned row=0;row<4;++row) for(unsigned value=0;value<256;++value) {
        uint32_t v=value*0x01010101u,e=thresholds[row],o=e+0x40404040u;
        unsigned top=ht_dots4(v,e,false)|(ht_dots4(v,o,true)>>1);
        unsigned bottom=ht_dots4(v,e,true)|(ht_dots4(v,o,false)>>1);
        ht_solid_dots[row][value]=(uint16_t)(top|(bottom<<8));
    }
}
static inline __attribute__((always_inline)) void ht_pack_lanes(uint32_t a,uint32_t c,
        uint32_t ar,uint32_t cr,uint32_t even,uint32_t odd,const uint16_t *solid,uint8_t *top,uint8_t *bottom) {
    if(a==c && ((a^(a>>8))&0x00ffffffu)==0 && ((ar^a)&255u)==0 && ((cr^a)&255u)==0) {
        unsigned pair=solid[a&255u];*top=(uint8_t)pair;*bottom=(uint8_t)(pair>>8);return;
    }
    uint32_t h=ht_average4(a,(a>>8)|(ar<<24));
    uint32_t v=ht_average4(a,c);
    uint32_t d=ht_average4(h,ht_average4(c,(c>>8)|(cr<<24)));
    *top=(uint8_t)(ht_dots4(a,even,false)|(ht_dots4(h,odd,true)>>1));
    *bottom=(uint8_t)(ht_dots4(v,even,true)|(ht_dots4(d,odd,false)>>1));
}
static __attribute__((optimize("O3"))) void ht_pack_mono(uint8_t *out,int stride) {
    bool word_output=(((uintptr_t)out|(unsigned)stride)&3u)==0;
    for(int y=0;y<HT_H;++y) {
        if(ht_framed && ht_visible_left[y]==HT_W/2 && ht_visible_left[ht_min(y+1,HT_H-1)]==HT_W/2) {
            memset(out+2*y*stride,255,HT_W/4);
            memset(out+(2*y+1)*stride,255,HT_W/4);
            continue;
        }
        int left=ht_framed?((ht_min(ht_visible_left[y],ht_visible_left[ht_min(y+1,HT_H-1)])-1)&~3):0;
        int right_edge=HT_W-left;
        const uint8_t *row=ht_scene+y*HT_W;
        const uint8_t *next=ht_scene+ht_min(y+1,HT_H-1)*HT_W;
        uint8_t *top=out+2*y*stride,*bottom=top+stride;
        if(left) {
            memset(top,255,left/4); memset(bottom,255,left/4);
            memset(top+right_edge/4,255,left/4); memset(bottom+right_edge/4,255,left/4);
        }
        /* Even-column rank*4+3, little-endian lane order. +3 converts the
         * original strict > rank*4+2 comparison to >=. Odd columns add 64
         * after removing their threshold's high bit; bottom swaps halves. */
        static const uint32_t thresholds[4]={0x3f0f3303u,0x1f2f1323u,0x37073b0bu,0x17271b2bu};
        uint32_t even=thresholds[y&3],odd=even+0x40404040u;
        int x=left;
        /* Align the destination before 32-bit stores. Source rows and bitmap
         * rows are aligned, but rounded-corner clipping can start mid-word. */
        for(;x<right_edge && (x&15);x+=4) {
            int nx=ht_min(x+4,HT_W-1);
            ht_pack_lanes(ht_load4(row+x),ht_load4(next+x),row[nx],next[nx],even,odd,ht_solid_dots[y&3],top+x/4,bottom+x/4);
        }
        for(;word_output && x+16<right_edge;x+=16) {
            uint32_t a=ht_load4(row+x),c=ht_load4(next+x),packed_top=0,packed_bottom=0;
            /* Four packed output bytes, one PSRAM store per physical row.
             * Carry lookahead words instead of doing extra byte reads. */
            for(unsigned lane=0;lane<4;++lane) {
                uint32_t ar=ht_load4(row+x+4*(lane+1)),cr=ht_load4(next+x+4*(lane+1));
                uint8_t t,b;ht_pack_lanes(a,c,ar,cr,even,odd,ht_solid_dots[y&3],&t,&b);
                packed_top|=(uint32_t)t<<(lane*8);packed_bottom|=(uint32_t)b<<(lane*8);
                a=ar;c=cr;
            }
            ht_store4(top+x/4,packed_top);ht_store4(bottom+x/4,packed_bottom);
        }
        for(;x<right_edge;x+=4) {
            int nx=ht_min(x+4,HT_W-1);
            ht_pack_lanes(ht_load4(row+x),ht_load4(next+x),row[nx],next[nx],even,odd,ht_solid_dots[y&3],top+x/4,bottom+x/4);
        }
        if((y&15)==15) ht_checkpoint();
    }
}
static void ht_pack_format(uint8_t *out,int stride,bool mono) {
    if(mono) { ht_pack_mono(out,stride); return; }
    static const uint8_t rank[8][8]={
        {41,8,52,7,45,15,47,4},{18,35,27,60,28,55,19,57},
        {54,1,49,9,62,3,40,14},{16,56,21,53,20,38,25,43},
        {59,12,42,6,39,10,34,5},{24,33,17,46,31,50,29,61},
        {36,2,58,11,63,0,51,13},{26,44,30,37,22,32,23,48}
    };
    /* Two logical samples produce four physical columns in both rows.
     * Reuse bilinear samples and remove per-pixel parity/edge branches. */
    for(int y=0;y<HT_H;++y) {
        if(ht_framed && ht_visible_left[y]==HT_W/2 && ht_visible_left[ht_min(y+1,HT_H-1)]==HT_W/2) {
            memset(out+2*y*stride,255,HT_W/2);
            memset(out+(2*y+1)*stride,255,HT_W/2);
            continue;
        }
        int left=ht_framed?((ht_min(ht_visible_left[y],ht_visible_left[ht_min(y+1,HT_H-1)])-1)&~3):0;
        int right_edge=HT_W-left;
        const uint8_t *row=ht_scene+y*HT_W;
        const uint8_t *next=ht_scene+ht_min(HT_H-1,y+1)*HT_W;
        uint8_t *top=out+y*2*stride,*bottom=top+stride;
        if(left) {
            memset(top,255,left/2); memset(bottom,255,left/2);
            memset(top+right_edge/2,255,left/2); memset(bottom+right_edge/2,255,left/2);
        }
        const uint8_t *rt=rank[(y*2)&7],*rb=rank[(y*2+1)&7];
        for(int x=left;x<right_edge;x+=2) {
            int nx=ht_min(HT_W-1,x+2),r=(x*2)&7;
            int a=row[x],b=(a+row[x+1])/2,c=row[x+1],d=(c+row[nx])/2;
            top[x/2]=(uint8_t)((ht_quantize(a,rt[r])<<6)|(ht_quantize(b,rt[r+1])<<4)|
                              (ht_quantize(c,rt[r+2])<<2)|ht_quantize(d,rt[r+3]));
            a=(a+next[x])/2; b=(b+(next[x]+next[x+1])/2)/2;
            c=(c+next[x+1])/2; d=(d+(next[x+1]+next[nx])/2)/2;
            bottom[x/2]=(uint8_t)((ht_quantize(a,rb[r])<<6)|(ht_quantize(b,rb[r+1])<<4)|
                                 (ht_quantize(c,rb[r+2])<<2)|ht_quantize(d,rb[r+3]));
        }
        if((y&15)==15) ht_checkpoint();
    }
}

static const uint8_t *ht_glyph(char ch) {
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t digits[10][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
        {0x62,0x51,0x49,0x49,0x46},{0x22,0x49,0x49,0x49,0x36},
        {0x18,0x14,0x12,0x7f,0x10},{0x2f,0x49,0x49,0x49,0x31},
        {0x3e,0x49,0x49,0x49,0x32},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x26,0x49,0x49,0x49,0x3e}
    };
    static const uint8_t letters[26][5] = {
        {0x7e,0x09,0x09,0x09,0x7e},{0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x26,0x49,0x49,0x49,0x32},{0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x7f,0x20,0x18,0x20,0x7f},{0x63,0x14,0x08,0x14,0x63},
        {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t arrow[5] = {0x00,0x41,0x22,0x14,0x08};
    static const uint8_t bar[5] = {0,0,0x7f,0,0};
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t dot[5] = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t plus[5] = {0x08,0x08,0x3e,0x08,0x08};
    static const uint8_t question[5] = {0x02,0x01,0x51,0x09,0x06};
    static const uint8_t comma[5] = {0x00,0x40,0x30,0x00,0x00};
    static const uint8_t apostrophe[5] = {0x00,0x00,0x03,0x00,0x00};
    static const uint8_t quote[5] = {0x00,0x03,0x00,0x03,0x00};
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch == '>') return arrow;
    if (ch == '|') return bar;
    if (ch == '-') return dash;
    if (ch == '.') return dot;
    if (ch == '/') return slash;
    if (ch == ':') return colon;
    if (ch == '+') return plus;
    if (ch == '?') return question;
    if (ch == ',') return comma;
    if (ch == '\'') return apostrophe;
    if (ch == '"') return quote;
    return blank;
}

static void ht_text_color(int x,int y,const char *text,int scale,int color) {
    for(;*text;++text,x+=6*scale) {
        const uint8_t *g=ht_glyph(*text);
        for(int a=0;a<5;++a) for(int b=0;b<7;++b)
            if(g[a]&(1u<<b)) ht_rect(ht_scene,x+a*scale,y+b*scale,scale,scale,color);
    }
}

static void ht_text(int x,int y,const char *text,int scale) { ht_text_color(x,y,text,scale,255); }

static void ht_pack(uint8_t *out,int stride) { ht_pack_format(out,stride,false); }

/* Two steady white lines in the lower dark margin. These rows remain within
 * the computed physical dirty range; no temporal/typewriter effect. */
static void ht_narration(const ht_game *game) {
    unsigned beat=ht_story_beat(game->story_x);
    ht_rect(ht_scene,57,232,366,19,255);
    for(int line=0;line<2;++line) {
        const char *text=ht_chapters[game->level].lines[beat][line];
        if(game->x/256>=2780) text=game->puzzle.solved?
            ht_puzzles[game->level].reveal[line]:
                (line?"START JOURNAL OR PAUSE THEN A / CONFIRM.":
                      "THE ANSWER IS HIDDEN IN THE THINGS LEFT BEHIND.");
        if(game->level==HT_LEVELS-1 && game->verdict)
            text=line?"THE WAY IS OPEN WHEN I HAVE READ WHAT I CHANGED.":
                (game->verdict==1?"I BROKE THE FEED. HER PLAN INCLUDED MY RESCUE.":
                                  "I COMPLETED THE FEED. THE PEN WRITES AS HER.");
        int x=(HT_W-(int)strlen(text)*6)/2, y=233+line*10;
        for(;*text;++text,x+=6) {
            const uint8_t *g=ht_glyph(*text);
            for(int a=0;a<5;++a) for(int b=0;b<7;++b)
                if(g[a]&(1u<<b)) ht_rect(ht_scene,x+a,y+b,1,1,0);
        }
    }
}

/* Small sharp interactive overlays; scenery/blur caches never depend on them. */
static void ht_draw_puzzle(const ht_game *game) {
    int camera=game->camera/256,top=ht_level_land(game->level)[9].top-game->camera_y/256;
    const ht_puzzle_definition *p=&ht_puzzles[game->level];
    for(int i=0;i<3;++i) {
        int x=HT_PUZZLE_FIRST+i*HT_PUZZLE_SPACING-camera;
        if(x< -30 || x>=HT_W+30) continue;
        ht_rect(ht_scene,x-3,top-27,6,27,255);
        if(p->kind==0) {
            int height=24+i*7;
            ht_rect(ht_scene,x-2,top-height-10,4,height+10,255);
            ht_disk(ht_scene,x,top-height,8,255);
            ht_rect(ht_scene,x-4,top-height-3,8,4,game->puzzle.solved?0:150);
        } else {
            ht_disk(ht_scene,x,top-30,10,255);
            ht_disk(ht_scene,x,top-30,7,0);
            char digit[2]={(char)('0'+game->puzzle.value[i]),0};
            ht_text(x-2,top-33,digit,1);
        }
        if(ht_puzzle_near(game)==i) ht_rect(ht_scene,x-12,top+3,24,2,0);
    }
    int x=HT_GOAL-camera;
    if(game->level==HT_LEVELS-1 && game->puzzle.solved) {
        ht_rect(ht_scene,x-18,top-48,36,48,255);
        ht_rect(ht_scene,x-15,top-45,30,37,0);
        ht_rect(ht_scene,x-1,top-45,2,37,255);
        ht_rect(ht_scene,x-12,top-38,8,19,game->verdict==2?255:140);
        ht_limb(ht_scene,x+4,top-39,x+11,top-18,1,1,255);
        if(game->verdict==1) ht_rect(ht_scene,x+2,top-45,13,37,255);
        ht_disk(ht_scene,x,top-56,4,255);
        if(game->verdict!=1) ht_disk(ht_scene,x,top-56,2,0);
        return;
    }
    if(!game->puzzle.solved || ht_final_locked(game)) {
        for(int i=-10;i<=10;i+=5) ht_rect(ht_scene,x+i,top-46,2,46,255);
        ht_rect(ht_scene,x-12,top-25,25,4,255);
    } else {
        ht_disk(ht_scene,x,top-61,5,255); ht_disk(ht_scene,x,top-61,3,0);
    }
}
static void ht_puzzle_prompt(const ht_game *game) {
    if(game->x/256<2780) return;
    const ht_puzzle_definition *p=&ht_puzzles[game->level];
    ht_rect(ht_scene,66,33,348,29,255);
    ht_text_color(74,37,p->name,1,0);
    if(game->puzzle.solved && game->level==HT_LEVELS-1) {
        ht_text_color(74,49,!game->verdict?"A / CONFIRM AT THE GLASS: BREAK OR COMPLETE":
            !game->verdict_read?"A / CONFIRM AT THE GLASS: READ THE CHANGED TESTIMONY":
            "PASSAGE OPEN - WALK ON TO BEGIN ANOTHER JOURNEY",1,0);
    } else if(game->puzzle.solved) ht_text_color(74,49,"PASSAGE OPEN - FOLLOW THE LIGHT",1,0);
    else if(p->kind==0) {
        char status[]="A / CONFIRM RING   NOTES 0/0";
        status[24]=(char)('0'+game->puzzle.progress); status[26]=(char)('0'+p->length);
        ht_text_color(74,49,game->puzzle.wrong?"WRONG NOTE - BEGIN AGAIN WITH THE CLUE":status,1,0);
    } else ht_text_color(74,49,p->kind==1?"A / CONFIRM TOGGLE NEARBY SWITCH   0 OFF 1 ON":"A / CONFIRM TURN NEARBY DIAL   0 1 2 3",1,0);
}

/* Tiny scraps/containers embedded in scenery. No flashing markers or distant
 * arrows; nearby interaction reveals the title without exposing the contents. */
static void ht_draw_evidence(const ht_game *game) {
    for(int item=0;item<3;++item) {
        int x=ht_evidence_x(game->level,item)-game->camera/256;
        int y=ht_level_land(game->level)[ht_evidence_platform(item)].top-game->camera_y/256;
        if(x< -12 || x>=HT_W+12) continue;
        ht_rect(ht_scene,x-6,y-10,12,10,255);
        if(item==1) ht_rect(ht_scene,x-8,y-12,16,3,255);
        else if(item==2) ht_limb(ht_scene,x-5,y-10,x+3,y-16,2,1,255);
        ht_rect(ht_scene,x-3,y-7,6,2,ht_evidence_found(game,game->level*3u+(unsigned)item)?130:0);
    }
}
static void ht_evidence_prompt(const ht_game *game) {
    int item=ht_evidence_near(game);
    if(item<0) return;
    ht_rect(ht_scene,66,33,348,29,255);
    ht_text_color(74,37,ht_evidence[game->level][item].title,1,0);
    ht_text_color(74,49,"A / CONFIRM INSPECT - A CLUE MAY BE HIDDEN HERE",1,0);
}
static void ht_draw_journal(const ht_game *game,unsigned page) {
    if(page>=HT_LEVELS*3u) return;
    unsigned level=page/3,item=page%3;
    ht_rect(ht_scene,50,62,380,144,255);
    char heading[]="JOURNAL 00 / 30";
    heading[8]=(char)('0'+(page+1)/10); heading[9]=(char)('0'+(page+1)%10);
    ht_text_color(66,72,heading,1,0);
    ht_text_color(66,86,ht_chapters[level].title,1,0);
    if(ht_evidence_found(game,page)) {
        const ht_evidence_record *record=&ht_evidence[level][item];
        ht_text_color(66,103,record->title,1,0);
        for(int line=0;line<4;++line) ht_text_color(66,119+line*13,record->lines[line],1,0);
    } else {
        ht_text_color(66,113,"NOT FOUND",1,0);
        ht_text_color(66,132,"LOOK FOR SMALL OBJECTS ALONG THE TRAIL.",1,0);
        ht_text_color(66,145,"APPROACH ON FOOT AND PRESS A / CONFIRM TO INSPECT.",1,0);
    }
    ht_text_color(66,184,"LEFT / RIGHT PAGES   A / CONFIRM OR A CLOSE",1,0);
}

#include "hollow_trail_traversal_draw.inc"
