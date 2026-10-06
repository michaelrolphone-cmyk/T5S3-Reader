/* Hollow Trail — deterministic ten-chapter route and final-door epilogue and PSRAM raster pipeline.
 * Baseline world/overlays are 480x270 with lower-resolution cached scenery;
 * the native A/B path rasterizes procedural geometry at the panel's 960x540
 * resolution. No textures, random frame noise, floating-point library,
 * framebuffer reads, or per-frame allocation. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "T5MathApi.h"
#include "epd_dither.h"
#define HT_W 480
#define HT_H 270
#define HT_NATIVE_SCALE 2
#define HT_NATIVE_W (HT_W*HT_NATIVE_SCALE)
#define HT_NATIVE_H (HT_H*HT_NATIVE_SCALE)
#define HT_NATIVE_PIXELS (HT_NATIVE_W*HT_NATIVE_H)
#define HT_SCENE_W (HT_W/2)
#define HT_SCENE_H (HT_H/2)
#define HT_SCENE_PIXELS (HT_SCENE_W*HT_SCENE_H)
#define HT_RECON_W (HT_SCENE_W/2)
#define HT_RECON_H ((HT_SCENE_H+1)/2)
#define HT_RECON_PIXELS (HT_RECON_W*HT_RECON_H)
/* Physical pixels are 2x logical: 18px black, fading through 38px. */
#define HT_BORDER 9
#define HT_FADE 19
#define HT_MASK_RADIUS 45
/* Keep the following low-resolution scene aligned for four-byte loads. */
#define HT_MASK_PIXELS ((HT_MASK_RADIUS*HT_MASK_RADIUS+15)&~15)
enum { HT_CAMERA_BASELINE, HT_CAMERA_NO_ROTATION, HT_CAMERA_NO_ZOOM, HT_CAMERA_OFF, HT_CAMERA_NATIVE, HT_CAMERA_MODES };
static unsigned ht_camera_mode;
static const char *const ht_camera_labels[]={"BASELINE", "ROTATION OFF", "ZOOM OFF", "BOTH OFF", "DETAIL 960X270"};
static bool ht_framed,ht_native_active,ht_native_foreground_half_y;
static int ht_world_scale=256; /* Draw-only projection; UI and cache geometry stay unscaled. */
static int ht_view_margin(void) {return (480*256/ht_world_scale-480)/2;}
static int ht_project_x(int x) {return 240+(x-240)*ht_world_scale/256;}
static int ht_project_y(int y) {return 135+(y-135)*ht_world_scale/256;}
static uint16_t ht_dirty_top,ht_dirty_height;
#define HT_PIXELS (HT_W * HT_H)
#define HT_TILE_W 256
#define HT_TILE_SLOTS 4
#define HT_LAYERS 4
#define HT_TILE_PIXELS (HT_TILE_W * HT_H)
#define HT_TILE_HALO ((HT_W-HT_TILE_W)/2)
#define HT_CACHE_BAND 64
#define HT_WARM_STEPS (HT_LAYERS*HT_TILE_SLOTS*(1+3*((HT_H+HT_CACHE_BAND-1)/HT_CACHE_BAND)))
/* Two focus maps cover alternating camera positions at the AI sampling grid. */
#define HT_SIMD_TABLE_BYTES (4u*6u*16u + 3u*16u)
#define HT_MEMORY (HT_SIMD_TABLE_BYTES + HT_RECON_PIXELS * 4u + HT_PIXELS * 6u + HT_LAYERS * HT_TILE_SLOTS * HT_TILE_PIXELS * 2u + HT_MASK_PIXELS + HT_SCENE_PIXELS + HT_RECON_PIXELS)
#define HT_NATIVE_MEMORY (HT_NATIVE_PIXELS * 2u)
#define HT_GOAL 3200
#define HT_STEP_MS 32u /* Fixed physics clock; scene direction varies world speed. */

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
enum { HT_FREE, HT_ROLL, HT_CRATE, HT_LADDER, HT_ROPE, HT_BOAT, HT_LEDGE, HT_CLIMB, HT_TREE, HT_PUSH, HT_SHUTTER, HT_WINDOW };
typedef struct {
    int ball_x,ball_y,crate_x,boat_x,rope_x,rope_v,ride_offset;
    int ball_vx,ball_vy,ball_roll,crate_y,crate_vx,crate_vy,boat_vx;
    int rope_bend_x,rope_bend_y,rope_bend_vx,rope_bend_vy; /* Q8 support flex */
    int rope_grip,rope_slide_v; /* Q8 distance along rope and tangential sliding speed */
    int rope_px[HT_ROPE_NODES],rope_py[HT_ROPE_NODES];
    int rope_ox[HT_ROPE_NODES],rope_oy[HT_ROPE_NODES];
    uint8_t support,bridge_open,forest_log_phase;
    bool forest_log_falling;
    uint8_t forest_push,forest_push_release;
    uint8_t push_hint; /* Draw-only half effort; never drives props or unlocks. */
    uint16_t forest_log_angle,forest_log_speed;
    uint8_t mode,ladder,ball_phase,ledge,climb,climb_phase,drop_cooldown;
    int ledge_side;
    uint8_t tree,branch_drop;
    int8_t tree_side;
    int16_t mill_scrape_left,mill_scrape_right; /* Grounded desk-leg trace, retained on checkpoint retry. */
    int mill_entry_left,mill_entry_right; /* Q8 grounded endpoints, never new terrain. */
    uint8_t mill_entry_phase;
    int window_entry_left,window_entry_right;
    uint8_t window_phase,window_open,window_touch;
    bool window_print;
    bool rope_awake;
} ht_traversal;
#include "hollow_trail_story.inc"
#include "hollow_trail_puzzles.inc"
#include "hollow_trail_evidence.inc"
typedef struct {
    int x, y, vx, vy; /* Q8, y is feet */
    int camera, camera_y, checkpoint, coyote, jump_buffer, facing, stride;
    unsigned ticks, laps, deaths, sway_phase;
    uint32_t rotation_phase; /* Q8: independently paced from breathing. */
    uint16_t pace,intimacy; /* Q8 walk speed and authored camera proximity. */
    uint16_t camera_mood,drop_zoom,vista; /* vista: 0..256 authored wide-shot envelope */
    int airborne_origin;
    uint8_t observation,observation_ticks,landing_ticks,landing_strength;
    int landing_x,landing_y;
    uint8_t scene_evidence; /* Finds in this chapter visit, separate from the archive. */
    uint16_t weather_age; /* Cue time advances only with active simulation. */
    uint16_t weather_amount; /* 0..256 narrative cue envelope. */
    int weather_drift; /* Q8, bounded screen-period displacement. */
    unsigned level; /* Laps count complete ten-chapter journeys. */
    int story_x; /* Furthest progress in this chapter, retained on death. */
    bool grounded;
    uint16_t waiting_view_tick; /* Draw snapshot only. */
    bool waiting_view_active;
    int8_t first_house_lean;
    bool isolator_view_active;
    uint8_t isolator_view_stage;
    uint16_t isolator_view_tick;
    uint8_t drawing_view_stage;
    uint16_t drawing_view_tick;
    uint8_t counts_view_stage;
    uint16_t counts_view_tick;
    bool care_tended,care_view_active; /* Optional physical scene; never an evidence/route gate. */
    uint8_t care_view_stage;
    uint16_t care_view_tick,care_view_ambient;
    bool food_view_active;
    uint8_t food_view_stage;
    uint16_t food_view_tick,food_view_ambient;
    uint8_t bed_view_stage;
    uint16_t bed_view_tick;
    uint8_t cabin_view_stage;
    uint16_t cabin_view_tick;
    bool carriage_view_active,carriage_view_boots;
    bool partition_view_active;
    uint16_t partition_view_tick;
    bool warming_view_active;
    uint16_t warming_view_tick;
    bool sleep_view_active;
    uint8_t sleep_view_stage;
    uint16_t sleep_view_tick;
    ht_traversal traversal;
    ht_puzzle_state puzzle;
    uint8_t verdict,last_verdict,endings; /* 0 undecided; 1 break; 2 complete; archive bitmask. */
    bool verdict_read;
    uint8_t door_stage; /* Chapter XI: independent of the ten evidence/puzzle slots. */
    uint16_t door_tick;
    bool door_notebook;
    uint8_t story_seen[HT_LEVELS]; /* Reached narration beats; retained with evidence. */
    uint32_t evidence; /* Thirty discoveries, retained across chapters this run. */
} ht_game;
static ht_game ht;
#define ht_land ht_level_land(ht.level)
/* Geometry belongs to a render snapshot, never live input/physics. */
static unsigned ht_geometry_level;
static const t5_math_api_v1 *ht_math;
static bool ht_simd_ready;
static const char *ht_simd_reason="NOT TESTED",*ht_expanded_reason="NOT TESTED";
static unsigned ht_simd_test_pattern,ht_simd_test_phase,ht_simd_test_byte;
static unsigned ht_simd_test_expected,ht_simd_test_actual;
static bool ht_simd_test_equal(const void *got,const void *want,unsigned size,unsigned pattern,unsigned phase) {
    const uint8_t *a=(const uint8_t *)got,*b=(const uint8_t *)want;
    for(unsigned i=0;i<size;++i)if(a[i]!=b[i]) {
        ht_simd_test_pattern=pattern;ht_simd_test_phase=phase;ht_simd_test_byte=i;
        ht_simd_test_expected=b[i];ht_simd_test_actual=a[i];return false;
    }
    return true;
}
static int16_t *ht_filter;
static uint8_t *ht_raw, *ht_near, *ht_wide, *ht_temp, *ht_scene, *ht_scene_low, *ht_native_a, *ht_native_b;
static uint8_t *ht_cached_near[HT_LAYERS][HT_TILE_SLOTS], *ht_cached_wide[HT_LAYERS][HT_TILE_SLOTS];
static int ht_cache_key[HT_LAYERS][HT_TILE_SLOTS];
static bool ht_cache_valid[HT_LAYERS][HT_TILE_SLOTS];
/* Depth 2 is owned by dynamic terrain, leaving its first near/wide pair free
 * for one complete forest backdrop (no allocation or memory-budget growth). */
#if 2*HT_TILE_PIXELS < HT_PIXELS
#error Forest backdrop exceeds the reserved cache pair
#endif
/* Composed chapters share the unused depth-2 cache pair; chapter is part of the key. */
static bool ht_forest_frame_valid;
static unsigned ht_composed_frame_level;
static int ht_forest_frame_x,ht_forest_frame_y,ht_forest_frame_scale;
static struct { bool active; int depth,key,phase,row; } ht_cache_job;
static bool ht_abort;
static unsigned ht_cache_builds;
static uint8_t *ht_corner_mask, *ht_low_scene, *ht_recon_scene;
static int16_t ht_visible_left[HT_H],ht_clear_left[HT_H];
static int16_t ht_native_visible_left[HT_NATIVE_H],ht_native_clear_left[HT_NATIVE_H];
/* Host callback polls/yields at bounded raster checkpoints; no drawing. */
static void (*ht_service)(void);
/* Optional host clock: inclusive stage costs include cooperative input/yield
 * time. No clock calls in the pixel loops and no firmware-specific imports. */
typedef struct {uint32_t background,world,post;} ht_render_timing;
static ht_render_timing ht_render_last;
static uint32_t (*ht_render_clock)(void);
static uint32_t ht_render_now(void) {return ht_render_clock?ht_render_clock():0;}
static int ht_min(int a,int b) { return a<b?a:b; }
static int ht_max(int a,int b) { return a>b?a:b; }
static int ht_clamp(int x,int lo,int hi) { return ht_min(hi,ht_max(lo,x)); }
static int ht_floor_div(int x,int n) { int q=x/n; return q-(x%n<0); }
static int ht_abs(int x) { return x<0?-x:x; }
static uint32_t ht_hash(uint32_t x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
#include "hollow_trail_terrain.inc"
#include "hollow_trail_trees.inc"
static void ht_checkpoint(void) { if(ht_service) ht_service(); }
#include "hollow_trail_neural_upscale.inc"

#include "hollow_trail_expanded_simd.inc"
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
static int ht_native_mask_alpha(int x,int y) {
    const int radius=HT_MASK_RADIUS*HT_NATIVE_SCALE;
    const int border=HT_BORDER*HT_NATIVE_SCALE,fade=HT_FADE*HT_NATIVE_SCALE;
    int ex=ht_min(x,HT_NATIVE_W-1-x),ey=ht_min(y,HT_NATIVE_H-1-y),edge;
    if(ex<radius && ey<radius) {
        int dx=radius-ex,dy=radius-ey;
        edge=radius-(int)ht_isqrt((unsigned)(dx*dx+dy*dy));
    } else edge=ht_min(ex,ey);
    return ht_clamp(edge-border,0,fade-border)*255/(fade-border);
}
static int ht_scene_width(void) {return ht_native_active?HT_NATIVE_W:HT_W;}
static int ht_scene_height(void) {return ht_native_active?HT_NATIVE_H:HT_H;}
static int ht_scene_visible_left(int y) {return ht_native_active?ht_native_visible_left[y]:ht_visible_left[y];}
static int ht_scene_clear_left(int y) {return ht_native_active?ht_native_clear_left[y]:ht_clear_left[y];}
static int ht_scene_mask_alpha(int x,int y) {return ht_native_active?ht_native_mask_alpha(x,y):ht_mask_alpha(x,y);}
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
    for(int y=0;y<HT_NATIVE_H;++y) {
        int x=0;
        while(x<HT_NATIVE_W/2 && !ht_native_mask_alpha(x,y)) ++x;
        ht_native_visible_left[y]=(int16_t)x;
        while(x<HT_NATIVE_W/2 && ht_native_mask_alpha(x,y)<255) ++x;
        ht_native_clear_left[y]=(int16_t)x;
    }
    /* Dirty rows are already physical in the native table. Keep one row of
     * halo so native and baseline submissions cover the same rounded edge. */
    int first=0,last=HT_NATIVE_H-1;
    while(first<HT_NATIVE_H && ht_native_visible_left[first]==HT_NATIVE_W/2) ++first;
    while(last>first && ht_native_visible_left[last]==HT_NATIVE_W/2) --last;
    ht_dirty_top=(uint16_t)ht_max(0,first-1);
    ht_dirty_height=(uint16_t)(last+1-ht_dirty_top);
}
/* Two exact focus maps cover the one-pixel camera-follow alternation without
 * recalculating radial division for every depth or settled frame. */
static uint16_t *ht_focus_map[2];
static int ht_focus_x[2],ht_focus_y[2];
static uint8_t ht_focus_step[2];
static bool ht_focus_valid[2];
static unsigned ht_focus_recent;
static const uint16_t *ht_focus_weights(int px,int py,int step,int width,int height) {
    for(unsigned i=0;i<2;++i) if(ht_focus_valid[i] && ht_focus_x[i]==px &&
       ht_focus_y[i]==py && ht_focus_step[i]==step) {
        ht_focus_recent=i;return ht_focus_map[i];
    }
    unsigned slot=ht_focus_recent^1u;uint16_t *map=ht_focus_map[slot];
    const int step2=step*step;
    for(int y=0;y<height;++y) {
        int dy=step*y-py,distance=px*px+dy*dy,delta=step2-2*step*px;
        for(int x=0;x<width;++x) {
            map[y*width+x]=(uint16_t)((unsigned)ht_clamp((distance-2500)/200,0,256));
            distance+=delta;delta+=2*step2;
        }
        if((y&15)==15) ht_checkpoint();
    }
    ht_focus_x[slot]=px;ht_focus_y[slot]=py;ht_focus_step[slot]=(uint8_t)step;
    ht_focus_valid[slot]=true;ht_focus_recent=slot;
    return map;
}
static void ht_pack_tables_init(void);
static void ht_simd_constants_init(uint8_t *memory);

static void ht_bind(uint8_t *memory) {
    ht_pack_tables_init();
    ht_simd_constants_init(memory+HT_MEMORY-HT_SIMD_TABLE_BYTES);
    ht_simd_reason=ht_expanded_reason="NOT TESTED";
    for(unsigned i=0;i<4;++i)ht_stage_reason[i]="NOT TESTED";
    ht_simd_test_pattern=ht_simd_test_phase=ht_simd_test_byte=0;
    ht_simd_test_expected=ht_simd_test_actual=0;
    ht_geometry_level=0; ht_simd_stage_ready=0; ht_expanded_ready=false; ht_simd_ready=false; ht_framed=false; ht_native_active=false; ht_native_foreground_half_y=false; ht_forest_frame_valid=false; ht_abort=false; ht_cache_job.active=false; ht_cache_builds=0;
    ht_raw=memory; ht_near=memory+HT_PIXELS; ht_wide=memory+2*HT_PIXELS;
    ht_scene_low=memory+3*HT_PIXELS; ht_scene=ht_scene_low; ht_temp=memory+4*HT_PIXELS; ht_filter=(int16_t *)ht_temp;
    uint8_t *cache=memory+6*HT_PIXELS;
    for(int d=0;d<HT_LAYERS;++d) for(int slot=0;slot<HT_TILE_SLOTS;++slot) {
        ht_cached_near[d][slot]=cache; cache+=HT_TILE_PIXELS;
        ht_cached_wide[d][slot]=cache; cache+=HT_TILE_PIXELS;
    }
    ht_corner_mask=cache; ht_low_scene=cache+HT_MASK_PIXELS; ht_mask_init();
    ht_focus_map[0]=(uint16_t *)(ht_low_scene+HT_SCENE_PIXELS);
    ht_focus_map[1]=ht_focus_map[0]+HT_RECON_PIXELS;
    /* Lookahead cache work can be suspended across visible frames. Its raw,
     * near and wide buffers remain live until the strip is published. */
    ht_recon_scene=(uint8_t *)(ht_focus_map[1]+HT_RECON_PIXELS);
    ht_native_a=ht_native_b=NULL;
    ht_focus_valid[0]=ht_focus_valid[1]=false;ht_focus_step[0]=ht_focus_step[1]=0;ht_focus_recent=0;
    memset(ht_cache_valid,0,sizeof(ht_cache_valid));
}
/* Optional extension owned by Hollow Trail's app allocation. Keeping it
 * outside HT_MEMORY preserves the existing cache/SIMD arena ABI and tests. */
static void ht_bind_native(uint8_t *memory) {
    ht_native_a=memory+HT_MEMORY;
    ht_native_b=ht_native_a+HT_NATIVE_PIXELS;
}
#define HT_SIGNAL_LOG_X 2170
#define HT_SIGNAL_ROOM_LEFT 2065
#define HT_SIGNAL_ROOM_RIGHT 2280
#define HT_SIGNAL_ROOM_HEIGHT 84
/* The rendered entry bay, sill and collision share the same upper roof. */
#define HT_SIGNAL_WINDOW_LEFT (ht_mechanics_by_level[1].ladders[2].x+17)
#define HT_SIGNAL_WINDOW_RIGHT (ht_mechanics_by_level[1].ladders[2].x+59)
#define HT_SIGNAL_WINDOW_STEPS 144
static int ht_signal_window_floor(void) {return ht_level_land(1)[7].top;}
static int ht_signal_window_sill(void) {return ht_signal_window_floor()-16;}
static int ht_signal_window_top(void) {return ht_signal_window_floor()-70;}

#define HT_WAITING_X 1745
static int ht_waiting_floor(void) {return ht_land_height(4,4,HT_WAITING_X);}
#define HT_DISTRIBUTION_X 2402
static int ht_distribution_floor(void) {return ht_land_height(2,7,HT_DISTRIBUTION_X);}
#define HT_STATION_TICKET_X 1180
static int ht_station_floor(void) {return ht_land_height(3,2,HT_STATION_TICKET_X);}
#define HT_STOVE_X 1820
static int ht_stove_floor(void) {return ht_land_height(2,5,HT_STOVE_X);}
static int ht_evidence_platform(unsigned level,int item) { return level==3 && item==0?2:level==2 && item==1?5:level==0?(item==0?0:item==1?3:7):1+item*3; }
static int ht_evidence_x(unsigned level,int item) {
    const ht_platform *p=&ht_level_land(level)[ht_evidence_platform(level,item)];
    return level==3 && item==0?HT_STATION_TICKET_X:level==2 && item==1?HT_STOVE_X:level==0?(item==0?140:item==1?1180:p->left+42):level==1 && item==0?535:level==1 && item==2?HT_SIGNAL_LOG_X:item==1?p->right-45:p->left+42;
}
static int ht_evidence_world_x(const ht_game *game,int item) {
    return game->level==0 && item==1?game->traversal.crate_x/256:ht_evidence_x(game->level,item);
}
static bool ht_mill_register_lit(const ht_game *game) {
    int x=game->traversal.crate_x/256;
    return game->level==0 && x>=HT_MILL_DESK_LIGHT_LEFT && x<=HT_MILL_DESK_LIGHT_RIGHT &&
        ht_abs(game->traversal.crate_y/256-ht_land_height(0,3,x))<=2;
}
static bool ht_evidence_found(const ht_game *game,unsigned page) {
    return page<HT_LEVELS*3u && (game->evidence&(1u<<page))!=0;
}
static int ht_platform_top(const ht_game *g,int i);
static int ht_evidence_near(const ht_game *game) {
    if(!game->grounded) return -1;
    for(int item=0;item<3;++item) {
        if(game->level==0 && item==1 && !ht_mill_register_lit(game))continue;
        if(ht_abs(game->x/256-ht_evidence_world_x(game,item))<=22 &&
           ht_abs(game->y/256-ht_surface_at(game,ht_evidence_platform(game->level,item),game->x/256))<=3) return item;
    }
    return -1;
}
static bool ht_schoolroom_near(const ht_game *g) {
    return g->level==1 && ht_evidence_near(g)==0;
}
static bool ht_signal_room_near(const ht_game *g) {
    return g->level==1 && ht_evidence_near(g)==2;
}
/* The last city narration belongs at the newly connected signal room, not
 * several roofs after the player has already watched its empty mechanism. */
static unsigned ht_story_beat_at(const ht_game *g) {
    bool reached_room=g->grounded && g->x/256>=HT_SIGNAL_ROOM_LEFT &&
        g->x/256<=HT_SIGNAL_ROOM_RIGHT && ht_abs(g->y/256-ht_level_land(1)[7].top)<=3;
    return g->level==1 && (g->story_seen[1]>=3 || reached_room)?2u:ht_story_beat(g->story_x);
}
static int ht_inspect(void) {
    int item=ht_evidence_near(&ht);
    if(item<0) return -1;
    int page=(int)ht.level*3+item;
    ht.evidence|=1u<<(unsigned)page;
    ht.scene_evidence|=(uint8_t)(1u<<(unsigned)item);
    if(ht.level==0 && item==1 && ht.traversal.mode==HT_CRATE) {
        ht.traversal.mode=HT_FREE;ht.traversal.crate_vx=0;ht.vx=0;
    } /* Reading releases the desk; returning cannot continue a held push. */
    return page;
}
#define HT_PUZZLE_FIRST 2900
#define HT_PUZZLE_SPACING 85
#define HT_PUZZLE_GATE (HT_GOAL-26)
static int ht_puzzle_near(const ht_game *game) {
    if(!game->grounded || ht_abs(game->y/256-ht_level_land(game->level)[9].top)>3) return -1;
    for(int i=0;i<ht_puzzle_controls(game->level);++i)
        if(ht_abs(game->x/256-(HT_PUZZLE_FIRST+i*HT_PUZZLE_SPACING))<=22) return i;
    return -1;
}
static bool ht_final_near(const ht_game *game) {
    return !game->door_stage && game->level==HT_LEVELS-1 && game->puzzle.solved && game->grounded &&
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
    ht_puzzle_operate(&ht.puzzle,ht.level,station);
    return true;
}
/* Silent-film cues: a discovery opens an atmospheric scene; the next
 * specified discovery resolves it. Location only supplies shelter inside it. */
typedef struct {int begin,end,rain,leaves,dust,sleet,wind,after,until,sky,lightning;} ht_climate;
static const ht_climate ht_climates[HT_LEVELS]={
    {450,1480,0,8,0,0,18,0,1,18,0},    /* Thread -> register: guidance becomes admission. */
    {1320,2160,24,0,0,0,24,1,2,38,0},  /* Burnt fuse -> watch log: the extinguished way. */
    {620,1740,0,0,16,0,25,0,1,16,0},   /* Lantern tag -> stove plate: cost of the gift. */
    {0,0,0,0,0,0,0,-1,-1,0,0},       /* Rail yard: silence after the cancelled train. */
    {0,0,0,0,0,0,0,-1,-1,0,0},       /* Marsh: stillness makes waiting visible. */
    {1490,2330,0,0,24,0,32,1,2,22,0},  /* Brake notice -> torn note: carrying their weight. */
    {0,0,0,0,0,0,0,-1,-1,0,0},       /* Gardens: shelter/confinement, no weather cue. */
    {1730,2580,48,0,0,0,40,1,2,68,1},  /* Garden pipe -> ridge circuit: dependence/control. */
    {540,1730,0,0,0,32,40,0,1,42,0},   /* Scarf -> wind shelter: doubt during the ascent. */
    {0,0,0,0,0,0,0,-1,-1,0,0},       /* Last light: stillness around the decision. */
};
static int ht_weather_target(const ht_game *g) {
    const ht_climate *c=&ht_climates[g->level];
    if(c->after<0) return 0;
    return (g->scene_evidence&(1u<<c->after)) && !(g->scene_evidence&(1u<<c->until))?256:0;
}
static void ht_weather_step(void) {
    int target=ht_weather_target(&ht);
    if(target && ht.weather_age<65535) ++ht.weather_age;
    if(ht.weather_amount<target) ht.weather_amount=(uint16_t)ht_min(target,ht.weather_amount+4);
    else if(ht.weather_amount>target) ht.weather_amount=(uint16_t)ht_max(target,ht.weather_amount-8);
}
static int ht_exposure(const ht_game *g) {
    const ht_climate *c=&ht_climates[g->level];
    int shelter=ht_clamp(ht_min(g->x/256-c->begin,c->end-g->x/256)*256/160,0,256);
    return shelter*g->weather_amount/256;
}
static int ht_lightning(const ht_game *g) {
    if(!ht_climates[g->level].lightning || !ht_weather_target(g) || g->weather_amount<192) return 0;
    int age=g->weather_age,phase=age>=380?age-380:age-160;
    if(phase<0 || phase>=8) return 0; /* Two authored pulses, never an endless storm loop. */
    return (phase<2?255:(8-phase)*36)*ht_exposure(g)/256;
}
static int ht_sky_ink(const ht_game *g) {
    static const uint8_t ambient[HT_LEVELS]={18,18,24,24,46,24,24,24,20,24};
    int light=ambient[g->level];
    if(g->level==4) light=14+32*ht_clamp(g->x/256-700,0,300)/300;
    int dark=light+ht_climates[g->level].sky*ht_exposure(g)/256;
    return dark*(255-ht_lightning(g))/255;
}
static int ht_wind(const ht_game *g) {
    unsigned phase=g->ticks&511u;
    int triangle=phase<128?(int)phase:phase<384?256-(int)phase:(int)phase-512;
    int gust=triangle*(256-ht_abs(triangle))*ht_climates[g->level].wind/(128*128);
    gust=gust*ht_exposure(g)/256;
    return g->ticks<96?gust*(int)g->ticks/96:gust;
}
/* Analog story tension: smooth authored spatial keys plus the active
 * discovery's emotional emphasis. No random camera motion or new raster pass. */
static int ht_mood_target(const ht_game *g) {
    static const uint8_t tension[HT_LEVELS][5]={
        {0,12,70,25,0},{0,30,115,40,0},{0,20,85,35,0},
        {0,20,100,30,0},{0,10,45,15,0},{0,35,130,40,0},
        {0,10,60,20,0},{0,35,208,120,0},{0,50,150,70,0},
        {0,15,70,110,20}
    };
    int x=ht_clamp(g->x/256,0,3199),span=x/800,part=x%800;
    int mood=tension[g->level][span]+((int)tension[g->level][span+1]-tension[g->level][span])*part/800;
    if(ht_weather_target(g)) mood+=ht_exposure(g)/3;
    return ht_clamp(mood,0,256);
}
static int ht_vista_target(const ht_game *g) {
    /* Quiet reveals: forest clearing, towers, distant rail cutting, reservoir,
     * escarpment and the last ascent. No chapter-wide miniature character. */
    static const int16_t scenes[HT_LEVELS][3]={
        {1640,2030,256},{1780,2320,224},{950,1470,160},{900,1240,128},
        {350,680,128},{1320,1610,192},{1230,1750,160},{1240,2420,256},
        {1130,1350,224},{1820,2410,192}
    };
    int x=g->x/256,a=scenes[g->level][0],b=scenes[g->level][1];
    int edge=ht_clamp(ht_min(x-a,b-x),0,160);
    return scenes[g->level][2]*edge/160;
}
static int ht_vista_lift(const ht_game *g) {
    static const uint8_t lift[HT_LEVELS]={24,56,0,24,12,36,0,80,52,56};
    return lift[g->level]*g->vista/256;
}
#include "hollow_trail_direction.inc"
static void ht_camera_step(bool moving) {
    if(moving || ht.observation_ticks) {
        int close=ht_intimacy_target(&ht);
        ht.intimacy=(uint16_t)(ht.intimacy+ht_clamp(close-ht.intimacy,-2,2));
        int wide=ht_vista_target(&ht);
        ht.vista=(uint16_t)(ht.vista+ht_clamp(wide-ht.vista,-4,4));
        int target=ht_mood_target(&ht),mood=ht.camera_mood;
        ht.camera_mood=(uint16_t)(mood+ht_clamp(target-mood,-2,2));
        ht.sway_phase+=5; /* Preserve the original breathing zoom exactly. */
        ht.rotation_phase=(ht.rotation_phase+213u+1067u*ht.camera_mood/256u)&262143u;
    }
    bool falling=!ht.grounded && !ht.traversal.mode && ht.vy>384 && ht.y>ht.airborne_origin+40*256;
    if(falling || ht.traversal.mode==HT_ROPE) ht.drop_zoom=(uint16_t)ht_min(256,ht.drop_zoom+16);
    else if(ht.drop_zoom) ht.drop_zoom=(uint16_t)ht_max(0,ht.drop_zoom-4);
    if(ht.grounded || ht.traversal.mode) ht.airborne_origin=ht.y;
}
static int ht_landmark_x(unsigned level,int scene) {
    const ht_platform *p=&ht_level_land(level)[scene?6:3];
    return p->left+(p->right-p->left)*2/3;
}
static int ht_observe_near(const ht_game *g) {
    if(!g->grounded || g->traversal.mode) return -1;
    for(int scene=0;scene<2;++scene)
        if(ht_abs(g->x/256-ht_landmark_x(g->level,scene))<34 &&
           ht_abs(g->y/256-ht_surface_at(g,scene?6:3,g->x/256))<3) return scene;
    return -1;
}
static bool ht_observe(void) {
    int scene=ht_observe_near(&ht);if(scene<0) return false;
    ht.observation=(uint8_t)(ht.level*2+scene+1);ht.observation_ticks=220;return true;
}
static void ht_spawn(bool restart) {
    if(!restart && ht.door_stage)return; /* No death/respawn can erase the held ending. */
    int16_t scrape_left=ht.traversal.mill_scrape_left,scrape_right=ht.traversal.mill_scrape_right;
    uint8_t bridge_open=restart?0:ht.traversal.bridge_open;
    uint8_t window_open=restart?0:ht.traversal.window_open;
    bool window_print=!restart && ht.traversal.window_print;
    uint8_t scene_evidence=restart?0:ht.scene_evidence;
    uint16_t weather_amount=restart?0:ht.weather_amount;
    uint16_t weather_age=restart?0:ht.weather_age;
    uint32_t evidence=ht.evidence;
    uint8_t verdict=restart?0:ht.verdict,last_verdict=ht.last_verdict,endings=ht.endings;
    bool verdict_read=!restart && ht.verdict_read;
    uint8_t seen[HT_LEVELS]; memcpy(seen,ht.story_seen,sizeof(seen));
    ht_puzzle_state puzzle=ht.puzzle;
    if(restart) ht_puzzle_reset(&puzzle,ht.level);
    unsigned laps=ht.laps, deaths=ht.deaths, level=ht.level;
    int cp=restart?0:ht.checkpoint, story_x=restart?0:ht.story_x;
    memset(&ht,0,sizeof(ht)); ht.laps=laps; ht.deaths=deaths; ht.level=level;
    ht.evidence=evidence; ht.puzzle=puzzle;
    ht.scene_evidence=scene_evidence;ht.weather_amount=weather_amount;ht.weather_age=weather_age;
    ht.verdict=verdict; ht.last_verdict=last_verdict; ht.endings=endings; ht.verdict_read=verdict_read;
    memcpy(ht.story_seen,seen,sizeof(seen));
    if(!ht.story_seen[level]) ht.story_seen[level]=1;
    ht.story_x=story_x; ht.checkpoint=cp; ht.x=(cp?ht_land[cp].left+35:95)*256;
    if(level==0 && cp==3)ht.x=(HT_MILL_SHUTTER_RIGHT+5)*256;
    ht.y=ht_land_height(level,cp,ht.x/256)*256; ht.airborne_origin=ht.y; ht.grounded=true; ht.facing=1;
    ht.pace=(uint16_t)ht_pace_target(&ht);ht.intimacy=(uint16_t)ht_intimacy_target(&ht);
    ht.camera=ht_clamp(ht.x/256-200,-120,HT_GOAL-260)*256;
    ht.camera_y=ht_clamp(ht.y/256-180,-300,100)*256;
    const ht_mechanics *m=&ht_mechanics_by_level[level];
    ht.traversal.bridge_open=bridge_open;
    if(level==1){ht.traversal.window_open=window_open;ht.traversal.window_print=window_print;}
    ht.traversal.forest_log_phase=level==0 && bridge_open==32?32:0;
    ht.traversal.ball_x=m->ball_x*256;ht.traversal.ball_y=(m->ball_floor-HT_BALL_RADIUS)*256;
    ht.traversal.crate_x=m->crate_x*256;ht.traversal.crate_y=m->crate_floor*256;
    if(level==0) {
        ht.traversal.mill_scrape_left=(int16_t)(!restart && scrape_left?scrape_left:HT_MILL_DESK_START+9);
        ht.traversal.mill_scrape_right=(int16_t)(!restart && scrape_right?scrape_right:HT_MILL_DESK_START+9);
    }
    ht.traversal.boat_x=m->boat_left*256;
    ht.traversal.rope_grip=m->rope_length*256;
    for(int i=0;i<HT_ROPE_NODES;++i) {
        ht.traversal.rope_px[i]=ht.traversal.rope_ox[i]=m->rope_anchor_x*256;
        ht.traversal.rope_py[i]=ht.traversal.rope_oy[i]=(m->rope_anchor_y+m->rope_length*i/(HT_ROPE_NODES-1))*256;
    }
}
#include "hollow_trail_final_door.inc"
#include "hollow_trail_traversal.inc"

static void ht_span(uint8_t *b,int x0,int x1,int y,int c) {
    if(b==ht_scene && ht_native_active && ht_native_foreground_half_y) {
        if(y<0 || y>=HT_H || x0>=HT_NATIVE_W || x1<0) return;
        x0=ht_max(x0,0);x1=ht_min(x1,HT_NATIVE_W-1);
        if(x1>=x0) {
            size_t count=(size_t)(x1-x0+1);
            uint8_t *top=b+(2*y)*HT_NATIVE_W+x0;
            memset(top,c,count);
            memcpy(top+HT_NATIVE_W,top,count);
        }
        return;
    }
    int width=(b==ht_scene && ht_native_active)?HT_NATIVE_W:HT_W;
    int height=(b==ht_scene && ht_native_active)?HT_NATIVE_H:HT_H;
    if(y<0 || y>=height || x0>=width || x1<0) return;
    x0=ht_max(x0,0); x1=ht_min(x1,width-1);
    if(x1>=x0) memset(b+y*width+x0,c,(size_t)(x1-x0+1));
}
static void ht_rect(uint8_t *b,int x,int y,int w,int h,int c) {
    if(w<=0 || h<=0) return;
    if(b==ht_scene && ht_world_scale!=256) {
        int right=ht_project_x(x+w),bottom=ht_project_y(y+h);
        x=ht_project_x(x);y=ht_project_y(y);w=ht_max(1,right-x);h=ht_max(1,bottom-y);
    }
    bool half_y=b==ht_scene && ht_native_active && ht_native_foreground_half_y;
    if(b==ht_scene && ht_native_active) {
        x*=HT_NATIVE_SCALE;w*=HT_NATIVE_SCALE;
        if(!half_y){y*=HT_NATIVE_SCALE;h*=HT_NATIVE_SCALE;}
    }
    int height=half_y?HT_H:((b==ht_scene && ht_native_active)?HT_NATIVE_H:HT_H);
    for(int row=ht_max(0,y);row<ht_min(height,y+h);++row) ht_span(b,x,x+w-1,row,c);
}
static void ht_triangle(uint8_t *b,int ax,int ay,int bx,int by,int cx,int cy,int c) {
    if(b==ht_scene && ht_world_scale!=256) {
        ax=ht_project_x(ax);ay=ht_project_y(ay);bx=ht_project_x(bx);by=ht_project_y(by);cx=ht_project_x(cx);cy=ht_project_y(cy);
    }
    bool half_y=b==ht_scene && ht_native_active && ht_native_foreground_half_y;
    if(b==ht_scene && ht_native_active) {
        ax*=HT_NATIVE_SCALE;bx*=HT_NATIVE_SCALE;cx*=HT_NATIVE_SCALE;
        if(!half_y){ay*=HT_NATIVE_SCALE;by*=HT_NATIVE_SCALE;cy*=HT_NATIVE_SCALE;}
    }
    if(ay>by){int t=ax;ax=bx;bx=t;t=ay;ay=by;by=t;}
    if(by>cy){int t=bx;bx=cx;cx=t;t=by;by=cy;cy=t;}
    if(ay>by){int t=ax;ax=bx;bx=t;t=ay;ay=by;by=t;}
    int height=half_y?HT_H:((b==ht_scene && ht_native_active)?HT_NATIVE_H:HT_H);
    for(int y=ht_max(ay,0);y<=ht_min(cy,height-1);++y) {
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
    if(b==ht_scene && ht_world_scale!=256) {x=ht_project_x(x);y=ht_project_y(y);r=ht_max(1,r*ht_world_scale/256);}
    if(b==ht_scene && ht_native_active && ht_native_foreground_half_y) {
        x*=HT_NATIVE_SCALE;
        for(int dy=-r;dy<=r;++dy) {
            int dx=r;while(dx>0 && dx*dx+dy*dy>r*r)--dx;
            ht_span(b,x-dx*HT_NATIVE_SCALE,x+dx*HT_NATIVE_SCALE,y+dy,c);
        }
        return;
    }
    if(b==ht_scene && ht_native_active) {x*=HT_NATIVE_SCALE;y*=HT_NATIVE_SCALE;r=ht_max(1,r*HT_NATIVE_SCALE);}
    for(int dy=-r;dy<=r;++dy) {
        int dx=r; while(dx>0 && dx*dx+dy*dy>r*r) --dx;
        ht_span(b,x-dx,x+dx,y+dy,c);
    }
}
#include "hollow_trail_foliage.inc"
#include "hollow_trail_organic_draw.inc"
static void ht_tree(uint8_t *b,int x,int base,int height,int width,uint32_t seed,int c) {
    if(ht_geometry_level==0) {ht_organic_tree(b,x,base,height,width,seed,c,true);return;}
    uint32_t h=ht_hash(seed);
    int lean=(int)(h%43)-21;
    int bend=(int)((h>>8)%19)-9;
    int kx=x+lean/3+bend, ky=base-height*52/100;
    int tx=x+lean,ty=base-height;
    ht_limb(b,x,base,kx,ky,width,ht_max(2,width*2/3),c);
    ht_limb(b,kx,ky,tx,ty,ht_max(2,width*2/3),1,c);
    if(b!=ht_scene) {
        ht_limb(b,x-width*3,base+4,x,base-height/6,1,width/2,c);
        ht_limb(b,x+width*2,base+5,x,base-height/7,1,width/2,c);
    } /* World trees receive terrain-sampled roots in ht_grounded_tree. */
    for(int j=0;j<5;++j) {
        ht_tree_branch branch=ht_tree_branch_shape(x,base,height,width,seed,j);
        h=ht_hash(seed+(uint32_t)j*137u);
        int side=branch.side,root=branch.x,at=branch.y;
        int ex=branch.ex,ey=branch.ey,mx=branch.mx,my=branch.my,reach=ht_abs(ex-root);
        ht_limb(b,root,at,mx,my,ht_max(1,width*(8-j)/13),ht_max(1,width/4),c);
        ht_limb(b,mx,my,ex,ey,ht_max(1,width/4),0,c);
        ht_limb(b,mx,my,mx+side*reach/3,my-height/7,ht_max(1,width/5),0,c);
        if(j<3) ht_limb(b,ex-side*reach/5,ey+3,ex+side*reach/6,ey-height/9,1,0,c);
        /* Foliage is grouped along branch ends, leaving air and a readable
         * branching structure instead of identical circular crowns. */
        if(ht_geometry_level==0 || ht_geometry_level==4 || ht_geometry_level==6) {
            for(int clump=0;clump<3;++clump) {
                int lx=ex-side*clump*reach/5,ly=ey+clump*3;
                ht_leaf_cluster(b,lx,ly,ht_max(7,height/15),h+(unsigned)clump*41u,c);
                if(j<2) ht_limb(b,lx,ly,lx-side*3,ly+height/14,0,0,c);
            }
        }
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
        int x=x0;
        /* Peel only the edge-extension region. Both direct indices in the
         * interior are valid even for a full-width or one-pixel ROI. */
        int left=ht_min(x1,radius),right=ht_min(x1,HT_W-radius-1);
        for(;x<left;++x) {
            ht_filter[y*HT_W+x]=ht_average(sum,reciprocal);
            sum+=row[ht_min(HT_W-1,x+radius+1)]-row[ht_max(0,x-radius)];
        }
        for(;x<right;++x) {
            ht_filter[y*HT_W+x]=ht_average(sum,reciprocal);
            sum+=row[x+radius+1]-row[x-radius];
        }

        for(;x<x1;++x) {
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
    bool fused=ht_expanded_active(HT_OPT_BLUR) && radius<=9;
    uint16_t k[16] __attribute__((aligned(16)));
    if(fused)for(int i=0;i<8;++i){k[i]=(uint16_t)((65536u+(unsigned)n-1)/(unsigned)n);k[8+i]=(uint16_t)n;}
    for(int y=y0;y<y1;++y) {
        const int16_t *add=ht_filter+ht_min(HT_H-1,y+radius+1)*HT_W;
        const int16_t *sub=ht_filter+ht_max(0,y-radius)*HT_W;
        uint8_t *row=dst+y*HT_W;

        if(fused) {
            /* Last output row must leave the running sums unchanged. */
            if(y+1==y1)add=sub;
            int x=x0;
            for(;x<x1 && (x&7);++x) {
                row[x]=ht_average(sums[x],reciprocal);
                sums[x]=(int16_t)(sums[x]+add[x]-sub[x]);
            }
            unsigned blocks=(unsigned)(x1-x)/8;
            if(blocks && !(((uintptr_t)(sums+x)|(uintptr_t)(add+x)|(uintptr_t)(sub+x))&15u)
                      && !((uintptr_t)(row+x)&7u)) {
                ht_blur_span(sums+x,add+x,sub+x,row+x,k,blocks);x+=(int)blocks*8;
            }
            for(;x<x1;++x) {
                row[x]=ht_average(sums[x],reciprocal);
                sums[x]=(int16_t)(sums[x]+add[x]-sub[x]);
            }
        } else {
            for(int x=x0;x<x1;++x)row[x]=ht_average(sums[x],reciprocal);
            if(y+1!=y1) {
                if(ht_math) {
                    (void)ht_math->add_s16(sums+x0,add+x0,sums+x0,(size_t)(x1-x0));
                    (void)ht_math->sub_s16(sums+x0,sub+x0,sums+x0,(size_t)(x1-x0));
                } else for(int x=x0;x<x1;++x)sums[x]=(int16_t)(sums[x]+add[x]-sub[x]);
            }
        }

        if((y&15)==15)ht_checkpoint();
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
    int spacing=depth==0?59:173, tone=depth==0?105:165;
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
        case 4: /* Open grotto water gives way to sunken shelters and willows. */
            if(j*spacing<620) {
                ht_triangle(ht_raw,x-45,HT_H,x+60,base-13,x+230,HT_H,38);
                break;
            }
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
#include "hollow_trail_depth.inc"
#include "hollow_trail_forest_light.inc"
static void ht_background(int depth,int offset) {
    if(depth==0 && ht_geometry_level>=3) {ht_distant_landscape(offset);return;}
    if(ht_geometry_level>=3) { ht_chapter_background(depth,offset); return; }
    if(ht_geometry_level==2) { ht_oil_background(depth,offset); return; }
    if(ht_geometry_level==1) { ht_city_background(depth,offset); return; }
    int tone=depth==0?100:180, base=depth==0?205:246;
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
    ht_forest_light(depth,offset);
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
static void ht_branches(int offset) {ht_close_framing(offset);}
static void ht_select_level(unsigned level) {
    if(ht_geometry_level==level) return;
    ht_geometry_level=level;
    ht_forest_frame_valid=false;
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
    if(depth==2)ht_forest_frame_valid=false;
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
/* Tile geometry depends on horizontal offset, not y. At stride four, a
 * width W covers at most ceil(4W/tile_width)+1 tiles, including partial ends.
 * Stack-only plan; no cache pointers survive this compositor call. */
static __attribute__((optimize("O3"))) void ht_composite_planned_ai(int depth,int offset,const uint16_t *weights,int fade) {
    struct ht_span {int x,count,column,slot;} spans[(4*HT_RECON_W+HT_TILE_W-1)/HT_TILE_W+1];
    const int border=HT_BORDER/4,height=(HT_SCENE_H-HT_BORDER/2+1)/2;
    uint16_t k[16] __attribute__((aligned(16)));
    bool fused=ht_expanded_active(HT_OPT_BLEND);
    if(fused)for(int i=0;i<8;++i){k[i]=(uint16_t)fade;k[i+8]=256;}
    int used=0;
    for(int x=border;x<HT_RECON_W-border;) {
        int world=offset+4*x,key=ht_floor_div(world,HT_TILE_W),column=world-key*HT_TILE_W;
        int count=ht_min(HT_RECON_W-border-x,(HT_TILE_W-column+3)/4);
        spans[used++]=(struct ht_span){x,count,column,ht_cache_slot(key)};
        x+=count;
    }
    for(int y=border;y<height;++y) {
        uint8_t *scene=ht_recon_scene+y*HT_RECON_W;
        const uint16_t *radials=weights+y*HT_RECON_W;
        for(int span=0;span<used;++span) {
            const struct ht_span *p=&spans[span];
            const uint8_t *near=ht_cached_near[depth][p->slot]+4*y*HT_TILE_W+p->column;
            const uint8_t *wide=ht_cached_wide[depth][p->slot]+4*y*HT_TILE_W+p->column;
            for(int i=0;i<p->count;++i) {
                int x=p->x+i;
                /* Bound aligned stores and source lookahead to this cache row;
                 * scalar prefix/tail preserve the original arithmetic. */
                if(fused && !(x&3)) {
                    int available=ht_min(p->count-i,(HT_TILE_W-p->column-4*i-15)/4);
                    unsigned blocks=available>0?(unsigned)available/8:0;
                    if(blocks) {
                        ht_blend_span(near+4*i,wide+4*i,radials+x,scene+x,k,blocks);
                        i+=(int)blocks*8-1;continue;
                    }
                }
                int radial=radials[x],v;
                if(radial==0) v=near[4*i];
                else if(radial==256) v=wide[4*i]*(256-fade)>>8;
                else {
                    v=(near[4*i]*256+((int)wide[4*i]-near[4*i])*radial)>>8;
                    v=v*(256-((radial*fade)>>8))>>8;
                }
                scene[x]=(uint8_t)ht_max(scene[x],v);
            }
        }
        if((y&7)==7) ht_checkpoint();
    }
}

static void ht_composite_cached_ai(int depth,int offset,int focus_x,int focus_y) {
    const uint16_t *weights=ht_focus_weights(focus_x,focus_y,4,HT_RECON_W,HT_RECON_H);
    ht_composite_planned_ai(depth,offset,weights,depth>=2?70:45);
}
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

#if HT_NN_EDGE_THRESHOLD != 80
#error "Update expanded SIMD edge gate when changing model threshold"
#endif
/* Build the exact bilinear reconstruction first with the same packed four-lane
 * arithmetic used by the original renderer, then spend neural MACs only on
 * strong 2x2 transitions. Source anchors at even/even coordinates never move. */
static __attribute__((optimize("O3"))) void ht_reconstruct_low_scene(void) {

    for(int y=0;y<HT_RECON_H;++y) {
        const uint8_t *row=ht_recon_scene+y*HT_RECON_W;
        const uint8_t *next=ht_recon_scene+ht_min(y+1,HT_RECON_H-1)*HT_RECON_W;
        uint8_t *top=ht_low_scene+2*y*HT_SCENE_W;
        uint8_t *bottom=2*y+1<HT_SCENE_H?top+HT_SCENE_W:NULL;
        int start=0;
        if(ht_expanded_active(HT_OPT_RECON) && bottom &&
           !(((uintptr_t)row|(uintptr_t)next)&7u) &&
           !(((uintptr_t)top|(uintptr_t)bottom)&15u)) {
            unsigned blocks=(HT_RECON_W-1)/16;
            ht_expand_span(row,next,top,bottom,blocks);start=(int)blocks*16;
        }
        for(int x=start;x<HT_RECON_W;x+=4) {
            uint32_t a=ht_load4(row+x),c=ht_load4(next+x);
            int nx=ht_min(x+4,HT_RECON_W-1);
            uint32_t b=(a>>8)|((uint32_t)row[nx]<<24);
            uint32_t d=(c>>8)|((uint32_t)next[nx]<<24);
            uint32_t h=ht_average4(a,b),v=ht_average4(a,c);
            uint32_t diagonal=ht_average4(h,ht_average4(c,d));
            ht_store4(top+2*x,ht_zip2(a,h));
            ht_store4(top+2*x+4,ht_zip2(a>>16,h>>16));
            if(bottom) {
                ht_store4(bottom+2*x,ht_zip2(v,diagonal));
                ht_store4(bottom+2*x+4,ht_zip2(v>>16,diagonal>>16));
            }
        }
        if((y&15)==15) ht_checkpoint();
    }
    for(int y=0;y+1<HT_RECON_H;++y) {
        int ym=y?y-1:0,yp=y+1;
        const uint8_t *r0=ht_recon_scene+ym*HT_RECON_W;
        const uint8_t *r1=ht_recon_scene+y*HT_RECON_W;
        const uint8_t *r2=ht_recon_scene+yp*HT_RECON_W;
        uint8_t *top=ht_low_scene+2*y*HT_SCENE_W,*bottom=top+HT_SCENE_W;

        uint8_t edges[(HT_RECON_W+15)&~15] __attribute__((aligned(16)));
        int edge_end=0;
        if(ht_expanded_active(HT_OPT_RECON) && !(((uintptr_t)r1|(uintptr_t)r2)&7u)) {
            unsigned blocks=(HT_RECON_W-1)/16;
            ht_edge_span(r1,r2,edges,blocks);edge_end=(int)blocks*16;
        }
        for(int x=0;x<HT_RECON_W;++x) {
            int xp=ht_min(x+1,HT_RECON_W-1);
            int a=r1[x],right=r1[xp],down=r2[x],down_right=r2[xp];
            if(x<edge_end) {if(!edges[x])continue;}
            else {
                int lo=ht_min(ht_min(a,right),ht_min(down,down_right));
                int hi=ht_max(ht_max(a,right),ht_max(down,down_right));
                if(hi-lo<HT_NN_EDGE_THRESHOLD)continue;
            }
            int xm=x?x-1:0;
            uint8_t patch[HT_NN_INPUTS]={
                r0[xm],r0[x],r0[xp],r1[xm],r1[x],r1[xp],r2[xm],r2[x],r2[xp]
            };
            int16_t residual[HT_NN_OUTPUTS];
            ht_nn_residual(patch,residual);
            top[2*x+1]=(uint8_t)ht_clamp((int)top[2*x+1]+residual[0],0,255);
            bottom[2*x]=(uint8_t)ht_clamp((int)bottom[2*x]+residual[1],0,255);
            bottom[2*x+1]=(uint8_t)ht_clamp((int)bottom[2*x+1]+residual[2],0,255);
        }

        if((y&7)==7) ht_checkpoint();
    }
}
static __attribute__((optimize("O3"))) void ht_upscale_scene(void) {
    for(int y=0;y<HT_SCENE_H;++y) {
        const uint8_t *row=ht_low_scene+y*HT_SCENE_W;
        const uint8_t *next=ht_low_scene+ht_min(y+1,HT_SCENE_H-1)*HT_SCENE_W;
        uint8_t *top=ht_scene+2*y*HT_W,*bottom=top+HT_W;
        int start=0;
        if(ht_expanded_active(HT_OPT_UPSCALE) && bottom &&
           !(((uintptr_t)row|(uintptr_t)next)&7u) &&
           !(((uintptr_t)top|(uintptr_t)bottom)&15u)) {
            unsigned blocks=(HT_SCENE_W-1)/16;
            ht_expand_span(row,next,top,bottom,blocks);start=(int)blocks*16;
        }
        for(int x=start;x<HT_SCENE_W;x+=4) {
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
/* Native mode allocates two physical-resolution grayscale surfaces. Low
 * frequency cached backdrops cross this bridge; vector/procedural geometry
 * that follows is rasterized directly at 960x540. */
static __attribute__((optimize("O3"))) void ht_expand_scene_native(const uint8_t *src,uint8_t *dst) {
    for(int y=0;y<HT_H;++y) {
        const uint8_t *row=src+y*HT_W,*next=src+ht_min(y+1,HT_H-1)*HT_W;
        uint8_t *top=dst+2*y*HT_NATIVE_W,*bottom=top+HT_NATIVE_W;
        for(int x=0;x<HT_W;++x) {
            int nx=ht_min(x+1,HT_W-1),a=row[x],b=row[nx],c=next[x],d=next[nx];
            top[2*x]=(uint8_t)a;top[2*x+1]=(uint8_t)((a+b+1)>>1);
            bottom[2*x]=(uint8_t)((a+c+1)>>1);
            bottom[2*x+1]=(uint8_t)((a+b+c+d+2)>>2);
        }
        if((y&15)==15)ht_checkpoint();
    }
}
#include "hollow_trail_character_pose.inc"
static int ht_weather_floor(const ht_game *g,int screen_x);
static void ht_character(int x,int feet,const ht_game *game) {
    /* Local mist separation keeps the player visible when a black trunk
     * crosses behind them. Never wash out the landing surface underfoot. */
    if(ht_native_active && ht_native_foreground_half_y) {
        int center=ht_project_x(x)*HT_NATIVE_SCALE,base=ht_project_y(feet);
        int radius=ht_max(1,25*ht_world_scale/256)*HT_NATIVE_SCALE,middle=ht_project_y(feet-19);
        for(int px=ht_max(0,center-radius);!(game->level==4 && game->camera/256<700) && px<=ht_min(HT_NATIVE_W-1,center+radius);++px) {
            int logical_px=px/HT_NATIVE_SCALE;
            int source=240+(logical_px-240)*256/ht_world_scale;
            int floor=ht_min(base,ht_project_y(ht_weather_floor(game,source))-1);
            for(int y=ht_max(0,ht_project_y(feet-43));y<ht_min(HT_H,floor);++y) {
                int dx=(px-center)*256/(ht_world_scale*HT_NATIVE_SCALE),dy=(y-middle)*256/ht_world_scale;
                int d=dx*dx+dy*dy;
                if(d<625) {
                    int ceiling=170+85*ht_clamp(d-100,0,525)/525;
                    int i=(2*y)*HT_NATIVE_W+px;
                    if(ht_scene[i]>ceiling)ht_scene[i]=(uint8_t)ceiling;
                    if(ht_scene[i+HT_NATIVE_W]>ceiling)ht_scene[i+HT_NATIVE_W]=(uint8_t)ceiling;
                }
            }
        }
    } else {
        int raster=ht_native_active?HT_NATIVE_SCALE:1,width=ht_scene_width(),height=ht_scene_height();
        int center=ht_project_x(x)*raster,base=ht_project_y(feet)*raster;
        int radius=ht_max(1,25*ht_world_scale/256)*raster,middle=ht_project_y(feet-19)*raster;
        for(int px=ht_max(0,center-radius);!(game->level==4 && game->camera/256<700) && px<=ht_min(width-1,center+radius);++px) {
            int logical_px=px/raster;
            int source=240+(logical_px-240)*256/ht_world_scale;
            int floor=ht_min(base,ht_project_y(ht_weather_floor(game,source))*raster-raster);
            for(int y=ht_max(0,ht_project_y(feet-43)*raster);y<ht_min(height,floor);++y) {
                int dx=(px-center)*256/(ht_world_scale*raster),dy=(y-middle)*256/(ht_world_scale*raster);
                int d=dx*dx+dy*dy;
                if(d<625) {
                    int ceiling=170+85*ht_clamp(d-100,0,525)/525;
                    int i=y*width+px;
                    if(ht_scene[i]>ceiling) ht_scene[i]=(uint8_t)ceiling;
                }
            }
        }
    }
    ht_character_walk(x,feet,game);
}

/* Darkness alpha is screen-fixed; it never changes the world or camera.
 * Apply once after all layers, with no multiply in the unshaded center. */
static inline unsigned ht_div255(unsigned value) {return (value+1+(value>>8))>>8;}
static void ht_vignette(void) {
    int width=ht_scene_width(),height=ht_scene_height();
    for(int y=0;y<height;++y) {
        uint8_t *row=ht_scene+y*width;
        int left=ht_scene_visible_left(y),clear=ht_scene_clear_left(y);
        memset(row,255,(size_t)left);memset(row+width-left,255,(size_t)left);
        for(int x=left;x<clear;++x) {
            int alpha=ht_scene_mask_alpha(x,y),right=width-1-x;
            uint32_t lanes=(uint32_t)(255-row[x])|((uint32_t)(255-row[right])<<16);
            lanes=lanes*(unsigned)alpha+0x007f007fu;
            row[x]=(uint8_t)(255-ht_div255(lanes&65535u));
            row[right]=(uint8_t)(255-ht_div255(lanes>>16));
        }
        if((y&31)==31)ht_checkpoint();
    }
    ht_framed=true;
}
/* Render-only affine camera. Nearest-neighbor sampling is the device-tested baseline;
 * world coordinates, collision and input bearings are never transformed.
 * Phase advances only with deliberate movement and freezes at rest. */
static int ht_sway_wave(unsigned phase) {
    int t=(int)(phase&1023u);
    int triangle=t<256?t:t<768?512-t:t-1024;
    return triangle*(512-ht_abs(triangle))/256;
}
/* A floor-bound room has a stable vertical. Ease the outdoor camera movement
 * away at the mill walls without touching physical motion or stored phases. */
static int ht_camera_motion_gain(const ht_game *g) {
    int x=g->x/256;
    if(g->level!=0 || x<=1080 || x>=1268 ||
       ht_abs(g->y/256-ht_land_height(0,3,x))>4)return 256;
    return 256-ht_clamp(ht_min(x-1080,1268-x)*256/40,0,256);
}
static int ht_camera_breathe(const ht_game *g) {
    int amount=(ht_sway_wave(g->sway_phase/2+768)+256)*4/3;
    return 4096-amount*ht_camera_motion_gain(g)/256;
}
/* Raster only the visible interior. At <=344/4096 off-diagonal rotation and inward
 * scaling, the nine-pixel border keeps the rounded source sample in bounds.
 * The vignette replaces everything outside this range immediately afterward. */
static void ht_camera_coefficients(const ht_game *g,int *turn,int *scale) {
    int gain=43+(int)g->camera_mood*213/256;
    *turn=ht_sway_wave(g->rotation_phase>>8)*43/32*gain/256*ht_camera_motion_gain(g)/256;
    int breathe=ht_camera_breathe(g);
    /* Widen toward the uncropped view during a true drop, then ease back
     * into the unchanged breathing phase. Retain the corner-safe crop. */
    breathe+=(4096-breathe)*g->drop_zoom/256;
    *scale=ht_min(breathe,4096-2*ht_abs(*turn));
}

/* Keep feet, supporting terrain and the complete boat above the caption.
 * Work on the frozen render camera, including both projection stages. No
 * physics, pace, zoom choice or narrative text is changed by this constraint. */
static void ht_frame_camera(ht_game *g,unsigned mode) {
    /* Swinging needs room for both the hands and the attachment above them.
     * Use the existing wide framing while attached, retaining vista scale. */
    if(g->traversal.mode==HT_ROPE) {g->intimacy=0;g->drop_zoom=256;}
    int turn=0,scale=4096;
    if(mode!=HT_CAMERA_OFF && (g->sway_phase || g->drop_zoom)) {
        ht_camera_coefficients(g,&turn,&scale);
        if(mode==HT_CAMERA_NO_ROTATION) {
            turn=0;scale=ht_camera_breathe(g);
            scale+=(4096-scale)*g->drop_zoom/256;
        } else if(mode==HT_CAMERA_NO_ZOOM)scale=4096;
    }
    /* Q8 coefficients keep the inverse projection inside 32-bit arithmetic. */
    int a=scale/16,b=turn/16,denom=a*a+b*b,world=ht_scene_scale(g);
    int half=(g->traversal.mode==HT_BOAT || g->traversal.support==3)?ht_boat_half(g)+16:24;
    int bottom=g->y/256+(g->traversal.mode==HT_ROPE?12:26);
    if(g->traversal.mode==HT_BOAT || g->traversal.support==3)bottom=ht_boat_top(g)/256+29;
    int sx=(g->x/256-g->camera/256-240)*world/256;
    int sy=(bottom-g->camera_y/256-135)*world/256;
    int lowest=135+256*(sy*a+sx*b+half*world/256*ht_abs(b))/ht_max(1,denom);
    if(lowest>211) {
        int shift=((lowest-211)*denom+world*a-1)/ht_max(1,world*a);
        g->camera_y+=shift*256;
    }
}

static __attribute__((optimize("O3"))) void ht_affine_into(const uint8_t *source,int turn,int scale) {
    int width=ht_scene_width(),height=ht_scene_height();
    int border=ht_native_active?HT_BORDER*HT_NATIVE_SCALE:HT_BORDER;
    for(int y=border;y<height-border;++y) {
        int left=ht_scene_visible_left(y),right=width-left;
        int u=(width/2)*4096+(left-width/2)*scale+(y-height/2)*turn;
        int v=(height/2)*4096-(left-width/2)*turn+(y-height/2)*scale;
        uint8_t *dst=ht_scene+y*width;
        for(int x=left;x<right;++x,u+=scale,v-=turn) {
            int ix=(u+2048)>>12,iy=(v+2048)>>12;
            dst[x]=source[iy*width+ix];
        }
        if((y&15)==15)ht_checkpoint();
    }
}
static void ht_sway_into(const uint8_t *source,unsigned phase) {
    int turn=ht_sway_wave(phase)*43/32;
    int scale=ht_min(4096-(ht_sway_wave(phase/2+768)+256)*4/3,4096-2*ht_abs(turn));
    ht_affine_into(source,turn,scale);
}
/* Axis-aligned zoom: one source row per output row, no per-pixel Y
 * projection or source-row multiplication. Keep baseline's rounding. */
static __attribute__((optimize("O3"))) void ht_zoom_into(const uint8_t *source,int scale) {
    int width=ht_scene_width(),height=ht_scene_height();
    int border=ht_native_active?HT_BORDER*HT_NATIVE_SCALE:HT_BORDER;
    for(int y=border;y<height-border;++y) {
        int sy=((height/2)*4096+(y-height/2)*scale+2048)>>12;
        const uint8_t *src=source+sy*width;
        uint8_t *dst=ht_scene+y*width;
        int left=ht_scene_visible_left(y),right=width-left;
        int u=(width/2)*4096+(left-width/2)*scale;
        for(int x=left;x<right;++x,u+=scale)dst[x]=src[(u+2048)>>12];
        if((y&15)==15)ht_checkpoint();
    }
}
/* With zoom genuinely disabled there is no safety crop. Clamp uncovered
 * corners to the source edge instead of reading outside the raster. */
static __attribute__((optimize("O3"))) void ht_rotation_into(const uint8_t *source,int turn) {
    int width=ht_scene_width(),height=ht_scene_height();
    int border=ht_native_active?HT_BORDER*HT_NATIVE_SCALE:HT_BORDER;
    for(int y=border;y<height-border;++y) {
        int left=ht_scene_visible_left(y),right=width-left;
        int u=left*4096+(y-height/2)*turn;
        int v=y*4096-(left-width/2)*turn;
        for(int x=left;x<right;++x,u+=4096,v-=turn)
            ht_scene[y*width+x]=source[ht_clamp((v+2048)>>12,0,height-1)*width+
                ht_clamp((u+2048)>>12,0,width-1)];
        if((y&15)==15)ht_checkpoint();
    }
}
static void ht_camera_into(const uint8_t *source,const ht_game *g,unsigned mode) {
    int turn,scale;ht_camera_coefficients(g,&turn,&scale);
    if(mode==HT_CAMERA_NO_ROTATION) {
        scale=ht_camera_breathe(g);
        scale+=(4096-scale)*g->drop_zoom/256;
        ht_zoom_into(source,scale);
    } else if(mode==HT_CAMERA_NO_ZOOM) ht_rotation_into(source,turn);
    else ht_affine_into(source,turn,scale);
}
/* Fixed particle budget, no allocations, frame noise or full-screen pass.
 * Weather shares the frozen world snapshot and pauses with simulation. */
/* Roofs and terrain occlude precipitation; it cannot streak through a
 * platform or continue visibly underground. Use the same physical segments. */
static int ht_weather_floor(const ht_game *g,int screen_x) {
    int x=screen_x+g->camera/256,floor=HT_H;
    const ht_mechanics *m=ht_mech(g);
    if(m->water_right && x>=m->water_left && x<m->water_right)
        floor=ht_min(floor,m->boat_deck+10-g->camera_y/256);
    for(int i=0;i<HT_PLATFORMS;++i)
        for(int part=0;part<3;++part) {
            int l,r,top;if(!ht_platform_piece(g,i,part,&l,&r,&top)) break;
            if(x>=l && x<r) floor=ht_min(floor,(part==2?top:ht_surface_at(g,i,x))-g->camera_y/256);
        }
    return floor;
}
/* Precipitation-only cover: ht_weather_floor also bounds character contrast. */
static int ht_rain_tank_weather_floor(const ht_game *g,int x0,int x1,int floor);
static void ht_weather(const ht_game *g) {
    const ht_climate *c=&ht_climates[g->level];int exposure=ht_exposure(g);
    if(!exposure || !(c->rain+c->leaves+c->dust+c->sleet)) return;
    int wind=ht_wind(g),drift=g->weather_drift/256;
    unsigned wet=(unsigned)((c->rain+c->sleet)*exposure/256);
    for(unsigned i=0;i<wet;++i) {
        uint32_t seed=ht_hash(i+g->level*97u);
        int x=(int)(seed%HT_W)+drift-g->camera/512;
        x=((x%HT_W)+HT_W)%HT_W;
        int y=(int)((seed/HT_W+(g->ticks%HT_H)*(i%2+3))%HT_H);
        int floor=ht_rain_tank_weather_floor(g,x,x+wind/9,ht_weather_floor(g,x));
        if(y<floor) ht_limb(ht_scene,x,y,x+wind/9,ht_min(floor-1,y+(c->sleet?2:4)+(int)(i%3)),0,0,c->sleet?180:125);
        if((i&15)==15) ht_checkpoint();
    }
    unsigned dry=(unsigned)((c->leaves+c->dust)*exposure/256);
    for(unsigned i=0;i<dry;++i) {
        uint32_t seed=ht_hash(i+1234u+g->level*37u);
        int flutter=(int)((g->ticks+i*11)%32);flutter=flutter<16?flutter:32-flutter;
        int x=(int)(seed%HT_W)+drift*4-g->camera/256+flutter/4;
        x=((x%HT_W)+HT_W)%HT_W;
        int y=(int)((seed/HT_W+(g->ticks/3)%HT_H)%HT_H)+flutter/5;
        if(y>=ht_weather_floor(g,x)-3) continue;
        if(g->level==0 && x+g->camera/256>=HT_MILL_SHUTTER_LEFT && x+g->camera/256<1212 &&
           y+g->camera_y/256>=133)continue; /* Leaf drift stays outside the mill interior. */
        int lean=wind<0?-1:1;
        if(c->leaves) ht_limb(ht_scene,x,y,x+lean*3,y+(flutter<8?1:-1),1,0,190);
        else ht_rect(ht_scene,x,y,1+(int)(i%3),1,i%2?70:155);
    }
    ht_checkpoint();
}
static void ht_draw_traversal(const ht_game *game);
static void ht_forest_foreground(const ht_game *game);
static void ht_city_observer_foreground(const ht_game *game);
static void ht_authored_foreground(const ht_game *game);
static void ht_draw_puzzle(const ht_game *game);
static void ht_draw_evidence(const ht_game *game);
static void ht_signal_window_draw(const ht_game *game,bool front);
/* At full opacity the grotto supplies every background pixel itself. */
static bool ht_grotto_opaque(const ht_game *g) {
    return g->level==4 && g->camera/256<=700;
}

#include "hollow_trail_final_door_draw.inc"
static void ht_render_scene_from(const ht_game *source,bool character) {
    if(source->door_stage) {ht_door_render(source,character);return;}
    memset(&ht_render_last,0,sizeof(ht_render_last));
    uint32_t stage=ht_render_now();
    ht_game game=*source;
    const bool native_mode=ht_camera_mode==HT_CAMERA_NATIVE;
    const unsigned camera_mode=native_mode?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode;
    ht_native_active=native_mode;
    ht_native_foreground_half_y=false;
    ht_scene=native_mode?ht_native_a:ht_scene_low;
    if(camera_mode==HT_CAMERA_NO_ZOOM || camera_mode==HT_CAMERA_OFF)game.vista=game.intimacy=0;
    ht_frame_camera(&game,camera_mode);
    const bool camera_pass=camera_mode!=HT_CAMERA_OFF && (game.sway_phase || game.drop_zoom);
    ht_select_level(game.level);
    int camera=game.camera/256,px=game.x/256-camera,py=(game.y-game.camera_y)/256-14;
    bool opaque=game.level!=4 || ht_grotto_opaque(&game);
    if(ht_abort)return;
    if(!opaque) {
        if(!ht_cache_visible(camera))return;
        memset(ht_recon_scene,ht_sky_ink(&game),HT_RECON_PIXELS);
        for(int depth=0;depth<HT_LAYERS;++depth)if(depth!=2)
            ht_composite_cached_ai(depth,ht_layer_offset(depth,camera),px,py);
        ht_reconstruct_low_scene();
    }
    uint8_t *output=ht_scene;
    if(camera_pass)ht_scene=native_mode?ht_native_b:ht_temp;
    if(!opaque) {
        if(native_mode) {
            uint8_t *native_target=ht_scene;
            ht_scene=ht_scene_low;ht_native_active=false;
            ht_upscale_scene();
            ht_scene=native_target;ht_native_active=true;
            ht_expand_scene_native(ht_scene_low,native_target);
        } else ht_upscale_scene();
    }
    if(!opaque && ht_lightning(&game)>80) {
        int x=340-game.camera/2048,y=22;
        for(int i=0;i<5;++i) {
            int nx=x+(i%2?-11:5),ny=y+12+i*3;
            int floor=ht_weather_floor(&game,x);
            if(y<floor)ht_limb(ht_scene,x,y,nx,ht_min(ny,floor-1),1,0,0);
            x=nx;y=ny;
        }
    }
    uint32_t world_start=ht_render_now();
    ht_render_last.background=world_start-stage;
    ht_world_scale=ht_scene_scale(&game);
    ht_draw_traversal(&game);
    ht_draw_puzzle(&game);
    if(character)ht_character(px,(game.y-game.camera_y)/256,&game);
    ht_weather(&game);
    ht_forest_foreground(&game);
    ht_signal_window_draw(&game,true);
    ht_city_observer_foreground(&game);
    ht_authored_foreground(&game);
    uint32_t post_start=ht_render_now();
    ht_render_last.world=post_start-world_start;
    ht_world_scale=256;
    ht_native_foreground_half_y=false;
    ht_scene=output;
    if(camera_pass)ht_camera_into(native_mode?ht_native_b:ht_temp,&game,camera_mode);
    ht_vignette();
    ht_render_last.post=ht_render_now()-post_start;
}
static void ht_render_scene(void) {ht_render_scene_from(&ht,true);}
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
static __attribute__((optimize("O3"))) void ht_pack_mono_native(uint8_t *out,int stride) {
    static const uint8_t rank[8][8]={
        {41,8,52,7,45,15,47,4},{18,35,27,60,28,55,19,57},
        {54,1,49,9,62,3,40,14},{16,56,21,53,20,38,25,43},
        {59,12,42,6,39,10,34,5},{24,33,17,46,31,50,29,61},
        {36,2,58,11,63,0,51,13},{26,44,30,37,22,32,23,48}
    };
    for(int y=0;y<HT_NATIVE_H;++y) {
        const uint8_t *row=ht_scene+y*HT_NATIVE_W;
        uint8_t *dst=out+y*stride;
        const uint8_t *r=rank[y&7];
        for(int byte=0;byte<HT_NATIVE_W/8;++byte) {
            int x=byte*8;uint8_t bits=0;
            for(int k=0;k<8;++k)
                if(row[x+k]>(int)r[(x+k)&7]*4+2)bits|=(uint8_t)(0x80u>>k);
            dst[byte]=bits;
        }
        if((y&15)==15)ht_checkpoint();
    }
}
static __attribute__((optimize("O3"))) void ht_pack_mono_legacy(uint8_t *out,int stride) {
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

#include "hollow_trail_simd.inc"

static __attribute__((optimize("O3"))) void ht_pack_mono_simd(uint8_t *out,int stride) {
    const uint8_t (*constants)[6][16]=ht_simd_constants;

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
        if(word_output && !(((uintptr_t)row|(uintptr_t)next)&15u) && x+16<right_edge) {
            unsigned blocks=(unsigned)(right_edge-x-1)/16u;
#if HT_SIMD_S3_COMPILED
            if(ht_simd_ready)
                ht_pack_simd_span_s3(row+x,next+x,top+x/4,bottom+x/4,&constants[y&3][0][0],blocks);
            else
#endif
                ht_pack_simd_span_c(row+x,next+x,top+x/4,bottom+x/4,&constants[y&3][0][0],blocks);
            x+=(int)blocks*16;
        }
        for(;x<right_edge;x+=4) {
            int nx=ht_min(x+4,HT_W-1);
            ht_pack_lanes(ht_load4(row+x),ht_load4(next+x),row[nx],next[nx],even,odd,ht_solid_dots[y&3],top+x/4,bottom+x/4);
        }

        if((y&15)==15) ht_checkpoint();
    }
}

static void ht_pack_mono(uint8_t *out,int stride) {
    if(ht_native_active) {ht_pack_mono_native(out,stride);return;}
    if(ht_simd_ready) ht_pack_mono_simd(out,stride);
    else ht_pack_mono_legacy(out,stride);
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

/* Two subdued gray lines in the lower dark margin. These rows remain within
 * the computed physical dirty range; no temporal/typewriter effect. */
#define HT_NARRATION_Y 230u
#define HT_NARRATION_HEIGHT 23u
static uint32_t ht_narration_key;
static void ht_narration_hash(const char *text) {
    for(;*text;++text){ht_narration_key^=(uint8_t)*text;ht_narration_key*=16777619u;}
    ht_narration_key^=255;ht_narration_key*=16777619u;
}
static void ht_narration(const ht_game *game) {
    ht_narration_key=2166136261u;
    if(game->door_stage) {ht_door_caption(game);return;}
    unsigned beat=ht_story_beat_at(game);
    ht_rect(ht_scene,57,230,366,23,255);
    for(int line=0;line<2;++line) {
        const char *text=ht_chapters[game->level].lines[beat][line];
        if(game->x/256>=2780) text=game->puzzle.solved?
            ht_puzzles[game->level].reveal[line]:
                (line?"":ht_puzzle_feedback(&game->puzzle));
        if(game->level==HT_LEVELS-1 && game->verdict)
            text=line?"THE WAY IS OPEN WHEN I HAVE READ WHAT I CHANGED.":
                (game->verdict==1?"I BROKE THE FEED. HER PLAN INCLUDED MY RESCUE.":
                                  "I COMPLETED THE FEED. THE PEN WRITES AS HER.");
        ht_narration_hash(text);
        int x=(HT_W-(int)strlen(text)*6)/2, y=233+line*10;
        for(;*text;++text,x+=6) {
            const uint8_t *g=ht_glyph(*text);
            for(int a=0;a<5;++a) for(int b=0;b<7;++b)
                if(g[a]&(1u<<b)) ht_rect(ht_scene,x+a,y+b,1,1,85);
        }
    }
}

#include "hollow_trail_cabinet_room_draw.inc"
#include "hollow_trail_isolator_draw.inc"
#include "hollow_trail_warming_draw.inc"
#include "hollow_trail_partition_draw.inc"
#include "hollow_trail_mechanism_draw.inc"
static void ht_draw_puzzle(const ht_game *game) {
    int top=ht_level_land(game->level)[9].top-game->camera_y/256;
    ht_draw_mechanisms(game,top);
    ht_isolator_box(game,game->isolator_view_active?ht_isolator_lid(game->isolator_view_stage,game->isolator_view_tick):0,
       game->isolator_view_active && !game->isolator_view_stage?ht_isolator_lift(game->isolator_view_tick):game->puzzle.stage?4:0);
    ht_warming_pipe(game,game->warming_view_active?ht_warming_amount(game->warming_view_tick):game->puzzle.stage?256:0);
    if(game->level==HT_LEVELS-1 && game->puzzle.solved) {
        ht_cabinet_case(game,top);
        return;
    }
    ht_draw_chapter_gate(game,top);
    ht_partition_response(game,top);
}
static void ht_puzzle_prompt(const ht_game *game) {
    if(ht_puzzle_near(game)<0) return;
    const ht_puzzle_definition *p=&ht_puzzles[game->level];
    ht_rect(ht_scene,66,33,348,29,255);
    ht_text_color(74,37,p->name,1,85);
    if(game->puzzle.solved && game->level==HT_LEVELS-1) {
        ht_text_color(74,49,!game->verdict?"A / CONFIRM AT THE GLASS: BREAK OR COMPLETE":
            !game->verdict_read?"A / CONFIRM AT THE GLASS: READ THE CHANGED TESTIMONY":
            "PASSAGE OPEN - WALK ON TO BEGIN ANOTHER JOURNEY",1,85);
    } else if(game->puzzle.solved) ht_text_color(74,49,game->puzzle.opening<48?"THE PASSAGE IS OPENING":"PASSAGE OPEN - FOLLOW THE LIGHT",1,85);
    else if(p->kind==HT_POINTS && ht_puzzle_near(game)<3) {
        ht_text_color(74,49,game->puzzle.selected==3?"A COUPLE REAR WAGON - RUN TESTS DEPARTURE":
            "A MOVE HERE - SAME ROAD RELEASES COUPLER",1,85);
    } else if(p->kind==HT_SIGNAL) {
        char status[]="A / CONFIRM RING   NOTES 0/0";
        status[25]=(char)('0'+game->puzzle.progress); status[27]=(char)('0'+p->length);
        ht_text_color(74,49,game->puzzle.feedback?ht_puzzle_feedback(&game->puzzle):game->puzzle.wrong?"THE SEQUENCE BREAKS":status,1,85);
    } else ht_text_color(74,49,"A / CONFIRM OPERATE THE NEARBY CONTROL",1,85);
}

/* Chapter I: one marked tree, not three unrelated clue props. The older
 * rounded wound faces the road; the new cloth stays caught on its hook after
 * inspection. Neither bark texture nor paper adds collision geometry. */
static void ht_marked_tree(int x,int ground,bool paper) {
    int trunk=x-8;
    ht_organic_snag(ht_scene,trunk,ground+5,245,16,813u,247);
    /* Unequal roots meet the existing terrain rather than a flat pedestal. */
    ht_limb(ht_scene,trunk,ground-6,x-30,ground+3,8,1,245);
    ht_limb(ht_scene,trunk+3,ground-7,x+18,ground+5,6,1,241);
    for(int n=0;n<4;++n) {
        int bx=trunk-9+n*5;
        ht_limb(ht_scene,bx,ground-160,bx+2,ground-118+n*5,0,0,221+n*4);
        ht_limb(ht_scene,bx+2,ground-108+n*4,bx-1,ground-66+n*3,0,0,221+n*4);
    }
    /* Rounded callus around the old wedge. The pale inner wood is quieter
     * than the new wool, and both remain physically adjacent. */
    ht_limb(ht_scene,trunk+5,ground-48,trunk+10,ground-38,3,3,174);
    ht_limb(ht_scene,trunk+10,ground-38,trunk+5,ground-30,3,3,174);
    ht_triangle(ht_scene,trunk+5,ground-46,trunk+10,ground-38,trunk+5,ground-32,207);
    /* Small incisions suggest the weathered date without inventing numerals
     * the novella never specifies. The full attribution is in the notebook. */
    for(int n=0;n<4;++n) {
        ht_limb(ht_scene,trunk-5+n*3,ground-61,trunk-4+n*3,ground-57,0,0,160);
        ht_limb(ht_scene,trunk-5+n*3,ground-57,trunk-3+n*3,ground-58,0,0,160);
    }
    for(int row=0;row<2;++row)for(int n=0;n<5;++n)
        ht_limb(ht_scene,trunk-6+n*3,ground-54+row*3,trunk-5+n*3,ground-53+row*3,0,0,181);
    ht_limb(ht_scene,trunk+4,ground-30,x+12,ground-35,2,1,244);
    ht_limb(ht_scene,x+12,ground-35,x+13,ground-39,1,0,244);
    /* Cloth folded into the new cut; a separate fine filament holds it. */
    ht_limb(ht_scene,trunk+8,ground-31,x+10,ground-34,0,0,129);
    ht_triangle(ht_scene,trunk+7,ground-32,x+9,ground-31,x+4,ground-14,99);
    ht_triangle(ht_scene,trunk+7,ground-32,x+4,ground-14,trunk+6,ground-19,114);
    ht_limb(ht_scene,x+2,ground-27,x+4,ground-16,0,0,179);
    /* Two backward bites interrupt the hem's otherwise neat run. */
    for(int n=0;n<5;++n)ht_limb(ht_scene,x+4,ground-28+n*2,x+5,ground-28+n*2,0,0,217);
    ht_limb(ht_scene,x+5,ground-26,x+3,ground-27,0,0,217);
    ht_limb(ht_scene,x+5,ground-20,x+3,ground-21,0,0,217);
    /* Narrow paper in the fold. Its socket is a drawing opening roadward,
     * never an invented physical lamp/socket attached to this tree. */
    if(paper) {
        ht_triangle(ht_scene,x+5,ground-27,x+13,ground-29,x+14,ground-22,64);
        ht_triangle(ht_scene,x+5,ground-27,x+14,ground-22,x+6,ground-21,64);
        ht_limb(ht_scene,x+8,ground-26,x+8,ground-23,0,0,212);
        ht_limb(ht_scene,x+8,ground-26,x+11,ground-26,0,0,212);
        ht_limb(ht_scene,x+8,ground-23,x+11,ground-23,0,0,212);
    }
}
/* Tiny scraps/containers embedded in scenery. No flashing markers or distant
 * arrows; nearby interaction reveals the title without exposing the contents. */
static void ht_draw_evidence(const ht_game *game) {
    for(int item=0;item<3;++item) {
        if((game->level==6 && (item==1 || item==2)) || (game->level==1 && item==2) || (game->level==0 && item==1) || (game->level==2 && item==1) || (game->level==3 && (item==0 || item==1)) || (game->level==2 && item==2) || (game->level==4 && item==1) || (game->level==5 && item==0))continue; /* Drawn on their actual scene furniture. */
        int x=ht_evidence_x(game->level,item)-game->camera/256;
        int y=ht_buried_base(game,ht_evidence_platform(game->level,item),ht_evidence_x(game->level,item),14)-game->camera_y/256;
        int extent=game->level==0 && item==0?34:12;
        if(x< -extent-ht_view_margin() || x>=HT_W+extent+ht_view_margin()) continue;
        /* Finds are objects within the scene, never hovering clue diagrams. */
        int ink=ht_evidence_found(game,game->level*3u+(unsigned)item)?150:95;
        if(game->level==0 && item==0) {
            ht_marked_tree(x,y,!ht_evidence_found(game,0));
        } else if(game->level==1 && item==0) {
            /* Brick-weighted schoolroom map on its child-sized desk. */
            ht_triangle(ht_scene,x-15,y-29,x+13,y-32,x+16,y-26,38);
            ht_triangle(ht_scene,x-15,y-29,x+16,y-26,x-13,y-24,38);
            ht_limb(ht_scene,x-9,y-27,x+8,y-29,0,0,160);
            for(int side=-1;side<=1;side+=2)for(int row=0;row<2;++row)
                ht_rect(ht_scene,x+side*13-2,y-30+row*4,4,2,189);
        } else if((game->level==0 && item==2) || (game->level==8 && item==0)) {
            if(game->level==0)ht_organic_snag(ht_scene,x-10,y,33,3,871u,255);
            else ht_limb(ht_scene,x-10,y,x-12,y-35,3,2,255);
            ht_limb(ht_scene,x-12,y-34,x+4,y-31,2,1,255);
            ht_limb(ht_scene,x+2,y-31,x+2,y-23,0,0,180);
            ht_triangle(ht_scene,x-2,y-18,x+6,y-18,x+2,y-24,170);
        } else if(game->level==2 || game->level==7) {
            ht_rect(ht_scene,x-10,y-18,20,18,235);
            ht_rect(ht_scene,x-7,y-15,14,10,140);
            ht_rect(ht_scene,x-3,y-21,6,3,230);
            ht_rect(ht_scene,x-5,y-10,7,2,ink);
        } else if(game->level==4 && item==1) {
            ht_limb(ht_scene,x-5,y,x-5,y-24,3,2,240);
            for(int knot=0;knot<3;++knot) ht_limb(ht_scene,x-8,y-16+knot*4,x-1,y-14+knot*4,1,1,ink);
        } else {
            /* A weathered document lies on a stump, seat or equipment chest. */
            if(game->level==0)ht_organic_snag(ht_scene,x,y,10,11,927u,235);
            else ht_rect(ht_scene,x-12,y-10,24,10,235);
            ht_limb(ht_scene,x-14,y-10,x+11,y-12,1,1,210);
            ht_triangle(ht_scene,x-6,y-12,x+4,y-15,x+8,y-11,ink);
            ht_limb(ht_scene,x-3,y-13,x+3,y-12,0,0,185);
        }
    }
}
static void ht_evidence_prompt(const ht_game *game) {
    int item=ht_evidence_near(game);
    if(item<0) return;
    ht_rect(ht_scene,66,33,348,29,255);
    ht_text_color(74,37,ht_evidence[game->level][item].title,1,85);
    ht_text_color(74,49,"A / CONFIRM INSPECT",1,85);
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

#include "hollow_trail_atmosphere_draw.inc"
#include "hollow_trail_traversal_draw.inc"

#include "hollow_trail_stove_study.inc"

#include "hollow_trail_station_study.inc"

#include "hollow_trail_distribution_study.inc"

#include "hollow_trail_waiting_study.inc"

#include "hollow_trail_pouch_study.inc"

#include "hollow_trail_sleep_study.inc"

#include "hollow_trail_partition_study.inc"
#include "hollow_trail_warming_study.inc"

#include "hollow_trail_isolator_study.inc"

#include "hollow_trail_first_house_study.inc"
#include "hollow_trail_carriage_study.inc"
#include "hollow_trail_signal_cabin_study.inc"
#include "hollow_trail_living_bed_study.inc"
#include "hollow_trail_counts_study.inc"

#include "hollow_trail_drawing_study.inc"

#include "hollow_trail_garden_night.inc"

#include "hollow_trail_food_study.inc"
#include "hollow_trail_care_study.inc"

#include "hollow_trail_privacy_study.inc"
