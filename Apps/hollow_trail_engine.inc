/* Hollow Trail — deterministic one-level platformer and PSRAM raster pipeline.
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
/* Physical pixels are 2x logical: 36px black, fading through 76px. */
#define HT_BORDER 18
#define HT_FADE 38
#define HT_MASK_RADIUS 90
#define HT_MASK_PIXELS (HT_MASK_RADIUS*HT_MASK_RADIUS)
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
#define HT_MEMORY (HT_PIXELS * 6u + HT_LAYERS * HT_TILE_SLOTS * HT_TILE_PIXELS * 2u + HT_MASK_PIXELS + HT_SCENE_PIXELS)
#define HT_GOAL 3200
#define HT_STEP_MS 32u /* Half-speed motion; preserve jump paths and level reachability. */

typedef struct { int left, right, top; } ht_platform;
static const ht_platform ht_land[] = {
    { -160, 390, 222 }, { 462, 725, 207 }, { 804, 1050, 190 },
    { 1118, 1300, 216 }, { 1374, 1535, 192 }, { 1610, 1800, 173 },
    { 1865, 2130, 208 }, { 2210, 2415, 188 }, { 2490, 2710, 214 },
    { 2780, 3410, 200 }
};
#define HT_PLATFORMS ((int)(sizeof(ht_land)/sizeof(ht_land[0])))
typedef struct {
    int x, y, vx, vy; /* Q8, y is feet */
    int camera, checkpoint, coyote, jump_buffer, facing, stride;
    unsigned ticks, laps, deaths;
    bool grounded;
} ht_game;
static ht_game ht;
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
static void ht_bind(uint8_t *memory) {
    ht_framed=false; ht_abort=false; ht_cache_job.active=false; ht_cache_builds=0;
    ht_raw=memory; ht_near=memory+HT_PIXELS; ht_wide=memory+2*HT_PIXELS;
    ht_scene=memory+3*HT_PIXELS; ht_temp=memory+4*HT_PIXELS; ht_filter=(int16_t *)ht_temp;
    uint8_t *cache=memory+6*HT_PIXELS;
    for(int d=0;d<HT_LAYERS;++d) for(int slot=0;slot<HT_TILE_SLOTS;++slot) {
        ht_cached_near[d][slot]=cache; cache+=HT_TILE_PIXELS;
        ht_cached_wide[d][slot]=cache; cache+=HT_TILE_PIXELS;
    }
    ht_corner_mask=cache; ht_low_scene=cache+HT_MASK_PIXELS; ht_mask_init();
    memset(ht_cache_valid,0,sizeof(ht_cache_valid));
}
static void ht_spawn(bool restart) {
    unsigned laps=ht.laps, deaths=ht.deaths;
    int cp=restart?0:ht.checkpoint;
    memset(&ht,0,sizeof(ht)); ht.laps=laps; ht.deaths=deaths;
    ht.checkpoint=cp; ht.x=(cp?ht_land[cp].left+35:95)*256;
    ht.y=ht_land[cp].top*256; ht.grounded=true; ht.facing=1;
    ht.camera=ht_clamp(ht.x/256-165,0,HT_GOAL-340)*256;
}
static void ht_step(int direction,bool jump_pressed,bool jump_held) {
    ++ht.ticks;
    if(jump_pressed) ht.jump_buffer=7;
    else if(ht.jump_buffer) --ht.jump_buffer;
    if(ht.grounded) ht.coyote=6; else if(ht.coyote) --ht.coyote;
    int target=direction*640; /* 78.125 world pixels/sec at 32ms per step */
    if(ht.vx<target) ht.vx=ht_min(target,ht.vx+100);
    if(ht.vx>target) ht.vx=ht_max(target,ht.vx-100);
    if(direction) ht.facing=direction;
    if(ht.jump_buffer && ht.coyote) {
        ht.vy=-1880; ht.grounded=false; ht.coyote=0; ht.jump_buffer=0;
    }
    if(!jump_held && ht.vy < -780) ht.vy=-780;
    int old_y=ht.y;
    ht.vy=ht_min(ht.vy+88,2400);
    ht.x=ht_clamp(ht.x+ht.vx,6*256,(HT_GOAL+60)*256);
    ht.y+=ht.vy;
    ht.grounded=false;
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&ht_land[i];
        if(ht.x+5*256<=p->left*256 || ht.x-5*256>=p->right*256) continue;
        if(ht.vy>=0 && old_y<=p->top*256 && ht.y>=p->top*256) {
            ht.y=p->top*256; ht.vy=0; ht.grounded=true;
            if(i==3 || i==6 || i==9) ht.checkpoint=i;
        } else if(ht.y>p->top*256+3*256 && old_y>p->top*256) {
            /* Solid cliff sides; no walking/jumping through a ledge. */
            if(ht.vx>0) ht.x=(p->left-5)*256;
            if(ht.vx<0) ht.x=(p->right+5)*256;
            ht.vx=0;
        }
    }
    if(ht.grounded) ht.stride+=ht_abs(ht.vx);
    if(ht.y>335*256) { ++ht.deaths; ht_spawn(false); return; }
    if(ht.x>=HT_GOAL*256 && ht.grounded) { ++ht.laps; ht_spawn(true); return; }
    int goal=ht_clamp(ht.x/256-165,0,HT_GOAL-340)*256;
    ht.camera+=(goal-ht.camera)/8;
}
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
static void ht_background(int depth,int offset) {
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
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&ht_land[i];
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
    int first=ht_floor_div(offset-300,910),last=ht_floor_div(offset+HT_W+300,910);
    for(int j=first;j<=last;++j) {
        int x=75+j*910-offset;
        ht_limb(ht_raw,x-110,-20,x+100,24,18,6,255);
        ht_limb(ht_raw,x+55,15,x+140,83,5,0,255);
        ht_limb(ht_raw,x+7,0,x-34,67,7,0,255);
    }
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
        int offset=ht_layer_offset(depth,camera);
        int first=ht_floor_div(offset+HT_BORDER,HT_TILE_W);
        int last=ht_floor_div(offset+HT_W-HT_BORDER-1,HT_TILE_W);
        for(int key=first;key<=last;++key) if(!ht_cache_ensure(depth,key)) return false;
    }
    return true;
}
/* Warm visible strips first, then one lookahead strip per plane. Called once
 * per host loop: no heap allocation and no unbounded full-level warmup. */
static bool ht_cache_prefetch(int camera,int direction) {
    if(ht_abort) return false;
    int chosen_depth=-1,chosen_key=0;
    for(int depth=0;depth<HT_LAYERS && chosen_depth<0;++depth) {
        int offset=ht_layer_offset(depth,camera);
        int first=ht_floor_div(offset+HT_BORDER,HT_TILE_W);
        int last=ht_floor_div(offset+HT_W-HT_BORDER-1,HT_TILE_W);
        for(int key=first;key<=last;++key) if(!ht_cache_has(depth,key)) {
            chosen_depth=depth; chosen_key=key; break;
        }
        if(chosen_depth<0) {
            int key=direction<0?first-1:last+1;
            if(!ht_cache_has(depth,key)) { chosen_depth=depth; chosen_key=key; }
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
static void ht_composite_cached(int depth,int offset,int focus_x,int focus_y) {
    const int fade=depth>=2?70:45;
    const int height=depth==3?45:HT_SCENE_H-HT_BORDER/2;
    int dy=HT_BORDER-focus_y,dy2=dy*dy;
    for(int y=HT_BORDER/2;y<height;++y) {
        uint8_t *scene=ht_low_scene+y*HT_SCENE_W;
        int distance=(HT_BORDER-focus_x)*(HT_BORDER-focus_x)+dy2;
        int delta=4+4*(HT_BORDER-focus_x);
        for(int x=HT_BORDER/2;x<HT_SCENE_W-HT_BORDER/2;) {
            int world=offset+2*x,key=ht_floor_div(world,HT_TILE_W),column=world-key*HT_TILE_W;
            int count=ht_min(HT_SCENE_W-HT_BORDER/2-x,(HT_TILE_W-column+1)/2),slot=ht_cache_slot(key);
            const uint8_t *near=ht_cached_near[depth][slot]+(2*y)*HT_TILE_W+column;
            const uint8_t *wide=ht_cached_wide[depth][slot]+(2*y)*HT_TILE_W+column;
            if(ht_dsp_composite && ht_dsp_available()) {
                ht_composite_dsp_span(scene+x,near,wide,count,distance,delta,fade);
                distance+=count*delta+4*count*(count-1); delta+=8*count;
                x+=count; continue;
            }
            for(int i=0;i<count;++i,++x) {
                int v;
                if(distance<=2500) v=near[2*i];
                else if(distance>=53700) v=wide[2*i]*(256-fade)>>8;
                else {
                    int radial=(distance-2500)/200;
                    v=(near[2*i]*256+((int)wide[2*i]-near[2*i])*radial)>>8;
                    v=v*(256-((radial*fade)>>8))>>8;
                }
                scene[x]=(uint8_t)ht_max(scene[x],v);
                distance+=delta; delta+=8;
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
static void ht_upscale_scene(void) {
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
    ht_limb(ht_scene,x,feet-17,x+f*5,feet-9,1,1,255);
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
static void ht_render_scene(void) {
    /* Input checkpoints may advance live physics. Freeze the rendered frame
     * so trees, focus and character all use one consistent game snapshot. */
    const ht_game game=ht;
    int camera=game.camera/256,px=game.x/256-camera,py=game.y/256-14;
    if(!ht_cache_visible(camera)) return;
    memset(ht_low_scene,0,HT_SCENE_PIXELS);
    for(int depth=0;depth<HT_LAYERS;++depth)
        ht_composite_cached(depth,ht_layer_offset(depth,camera),px,py);
    ht_upscale_scene();
    ht_character(px,game.y/256,&game);
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
static void ht_pack_mono(uint8_t *out,int stride) {
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
        for(int x=left;x<right_edge;x+=4) {
            uint32_t a=ht_load4(row+x),c=ht_load4(next+x);
            int nx=ht_min(x+4,HT_W-1);
            uint32_t right=(a>>8)|((uint32_t)row[nx]<<24);
            uint32_t below=(c>>8)|((uint32_t)next[nx]<<24);
            uint32_t h=ht_average4(a,right),v=ht_average4(a,c);
            uint32_t d=ht_average4(h,ht_average4(c,below));
            top[x/4]=(uint8_t)(ht_dots4(a,even,false)|(ht_dots4(h,odd,true)>>1));
            bottom[x/4]=(uint8_t)(ht_dots4(v,even,true)|(ht_dots4(d,odd,false)>>1));
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
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t dot[5] = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t plus[5] = {0x08,0x08,0x3e,0x08,0x08};
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch == '-') return dash;
    if (ch == '.') return dot;
    if (ch == '/') return slash;
    if (ch == ':') return colon;
    if (ch == '+') return plus;
    return blank;
}

static void ht_text(int x,int y,const char *text,int scale) {
    for(;*text;++text,x+=6*scale) {
        const uint8_t *g=ht_glyph(*text);
        for(int a=0;a<5;++a) for(int b=0;b<7;++b)
            if(g[a]&(1u<<b)) ht_rect(ht_scene,x+a*scale,y+b*scale,scale,scale,255);
    }
}

static void ht_pack(uint8_t *out,int stride) { ht_pack_format(out,stride,false); }
