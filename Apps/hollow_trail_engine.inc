/* Hollow Trail — deterministic one-level platformer and PSRAM raster pipeline.
 * Coordinates are 480x270, output 960x540. No textures, random frame noise,
 * floating-point library, framebuffer reads, or per-frame allocation. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "T5MathApi.h"
#define HT_W 480
#define HT_H 270
#define HT_PIXELS (HT_W * HT_H)
#define HT_MEMORY (HT_PIXELS * 10u)
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
static uint8_t *ht_cached_near[3], *ht_cached_wide[3];
static int ht_cache_key[3];
static bool ht_cache_valid[3];
/* Host callback polls/yields at bounded raster checkpoints; no drawing. */
static void (*ht_service)(void);
static int ht_min(int a,int b) { return a<b?a:b; }
static int ht_max(int a,int b) { return a>b?a:b; }
static int ht_clamp(int x,int lo,int hi) { return ht_min(hi,ht_max(lo,x)); }
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
static void ht_bind(uint8_t *memory) {
    ht_raw=memory; ht_near=memory+HT_PIXELS; ht_wide=memory+2*HT_PIXELS;
    ht_temp=memory+8*HT_PIXELS; ht_filter=(int16_t *)ht_temp; ht_scene=memory+3*HT_PIXELS;
    ht_cached_near[0]=ht_near; ht_cached_wide[0]=ht_wide;
    for(int i=1;i<3;++i) {
        ht_cached_near[i]=memory+(2+2*i)*HT_PIXELS;
        ht_cached_wide[i]=memory+(3+2*i)*HT_PIXELS;
    }
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
static void ht_blur_horizontal(const uint8_t *src,int radius,uint32_t reciprocal,int x0,int x1) {
    for(int y=0;y<HT_H;++y) {
        const uint8_t *row=src+y*HT_W; int sum=0;
        for(int k=-radius;k<=radius;++k) sum+=row[ht_clamp(x0+k,0,HT_W-1)];
        for(int x=x0;x<x1;++x) {
            ht_filter[y*HT_W+x]=ht_average(sum,reciprocal);
            sum+=row[ht_min(HT_W-1,x+radius+1)]-row[ht_max(0,x-radius)];
        }
        if((y&31)==31) ht_checkpoint();
    }
}
static void ht_blur_region(const uint8_t *src,uint8_t *dst,int radius,int x0,int x1) {
    if(!radius) {
        for(int y=0;y<HT_H;++y) {
            if(ht_math) (void)ht_math->copy_bytes(dst+y*HT_W+x0,src+y*HT_W+x0,(size_t)(x1-x0));
            else memcpy(dst+y*HT_W+x0,src+y*HT_W+x0,(size_t)(x1-x0));
        }
        return;
    }
    int n=radius*2+1;
    uint32_t reciprocal=(0x1000000u+(uint32_t)n-1u)/(uint32_t)n;
    ht_blur_horizontal(src,radius,reciprocal,x0,x1);
    /* Keep vertical sums in 960 bytes of stack and visit PSRAM by rows.
     * Column-strided reads would evict cache lines on every sample. */
    int16_t sums[HT_W] __attribute__((aligned(16)))={0};
    for(int k=-radius;k<=radius;++k) {
        const int16_t *row=ht_filter+ht_clamp(k,0,HT_H-1)*HT_W;
        for(int x=x0;x<x1;++x) sums[x]+=row[x];
    }
    for(int y=0;y<HT_H;++y) {
        const int16_t *add=ht_filter+ht_min(HT_H-1,y+radius+1)*HT_W;
        const int16_t *sub=ht_filter+ht_max(0,y-radius)*HT_W;
        uint8_t *row=dst+y*HT_W;
        for(int x=x0;x<x1;++x) {
            row[x]=ht_average(sums[x],reciprocal);
        }
        if(ht_math) {
            /* 0..4845 sums, 0..255 samples: even the temporary addition
             * fits int16_t exactly. SIMD handles eight columns at a time. */
            (void)ht_math->add_s16(sums+x0,add+x0,sums+x0,(size_t)(x1-x0));
            (void)ht_math->sub_s16(sums+x0,sub+x0,sums+x0,(size_t)(x1-x0));
        } else for(int x=x0;x<x1;++x) sums[x]=(int16_t)(sums[x]+add[x]-sub[x]);
        if((y&15)==15) ht_checkpoint();
    }
}
static void ht_shift_layer(uint8_t *buffer,int delta) {
    int count=HT_W-ht_abs(delta);
    for(int y=0;y<HT_H;++y) {
        uint8_t *row=buffer+y*HT_W;
        memmove(row+ht_max(-delta,0),row+ht_max(delta,0),(size_t)count);
        if((y&31)==31) ht_checkpoint();
    }
}
static void ht_blur_edges(const uint8_t *src,uint8_t *dst,int radius,int margin) {
    if(margin*2>=HT_W) ht_blur_region(src,dst,radius,0,HT_W);
    else {
        ht_blur_region(src,dst,radius,0,margin);
        ht_blur_region(src,dst,radius,HT_W-margin,HT_W);
    }
}
static void ht_prepare_layer(int depth,int key) {
    static const int radius[3]={3,1,0}, spread[3]={9,6,4};
    ht_near=ht_cached_near[depth]; ht_wide=ht_cached_wide[depth];
    int delta=key-ht_cache_key[depth];
    if(ht_cache_valid[depth] && delta==0) return;
    /* Parallax scenery is a pure integer translation. Reuse its exact
     * filtered interior; recalculate exposed columns AND both old/new blur
     * boundary footprints. This preserves viewport edge extension exactly.
     * Foreground framing has another parallax speed, so never shift it. */
    int spacing=depth==0?103:169;
    bool same_geometry=(key-150)/spacing==(ht_cache_key[depth]-150)/spacing;
    /* Match existing deterministic object culling, including a tower whose
     * parent building just left the viewport margin. */
    int period=depth==0?680:1040;
    for(int j=0;j<6 && same_geometry;++j) {
        int old_x=330+j*period-ht_cache_key[depth],new_x=330+j*period-key;
        same_geometry=(old_x>=-100 && old_x<=HT_W+100)==(new_x>=-100 && new_x<=HT_W+100);
    }
    if(depth<2 && ht_cache_valid[depth] && same_geometry && ht_abs(delta)<HT_W/3) {
        ht_shift_layer(ht_near,delta); ht_shift_layer(ht_wide,delta);
        ht_blur_edges(ht_raw,ht_near,radius[depth],ht_abs(delta)+radius[depth]);
        ht_blur_edges(ht_near,ht_wide,spread[depth],ht_abs(delta)+radius[depth]+spread[depth]);
    } else {
        ht_blur_region(ht_raw,ht_near,radius[depth],0,HT_W);
        ht_blur_region(ht_near,ht_wide,spread[depth],0,HT_W);
    }
    ht_cache_key[depth]=key; ht_cache_valid[depth]=true;
}
static void ht_composite(int depth,int focus_x,int focus_y) {
    /* Filter caches contain scenery only. Recompute character-centered radial
     * mixing/fade every frame, including when its camera has not moved. */
    for(int y=0;y<HT_H;++y) {
        int dy=y-focus_y;
        for(int x=0;x<HT_W;++x) {
            int dx=x-focus_x, i=y*HT_W+x;
            int radial=ht_clamp((dx*dx+dy*dy-2500)/200,0,256);
            int v=(ht_near[i]*(256-radial)+ht_wide[i]*radial)>>8;
            v=v*(256-radial*(depth==2?70:45)/256)>>8;
            /* Premultiplied silhouette over the previous layer. */
            ht_scene[i]=(uint8_t)ht_max(ht_scene[i],v);
        }
        if((y&31)==31) ht_checkpoint();
    }
}
static void ht_background(int depth,int camera) {
    int parallax=depth==0?22:53, offset=camera*parallax/100;
    int tone=depth==0?85:170, base=depth==0?219:246;
    /* Trace the same hill boundary, then fill contiguous rows. Drawing
     * one-pixel-wide vertical rectangles thrashed PSRAM/cache and called
     * memset tens of thousands of times per frame. ht_temp is scratch here. */
    int lowest=HT_H;
    for(int x=0;x<HT_W;++x) {
        int wx=x+offset, cell=wx/220, t=wx%220;
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
    int start=(offset-150)/spacing;
    for(int j=start;j<start+7;++j) {
        uint32_t h=ht_hash((uint32_t)(j+80+depth*59));
        int x=j*spacing-offset+(int)(h%31),height=(depth==0?101:208)+(int)(h%90);
        ht_tree(ht_raw,x,base,height,depth==0?3:10,h,tone);
    }
    /* Abandoned pitched-roof mill and a distant watchtower. */
    int period=depth==0?680:1040;
    for(int j=0;j<6;++j) {
        int x=330+j*period-offset;
        if(x< -100 || x>HT_W+100) continue;
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
    }
}
static void ht_foreground(int camera) {
    for(int i=0;i<HT_PLATFORMS;++i) {
        const ht_platform *p=&ht_land[i];
        int l=p->left-camera,r=p->right-camera;
        if(r<0 || l>=HT_W) continue;
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
    }
    /* Nearest framing branches move faster than the playable plane. */
    int x=75-camera*123/100;
    while(x< -240) x+=910;
    ht_limb(ht_raw,x-110,-20,x+100,24,18,6,255);
    ht_limb(ht_raw,x+55,15,x+140,83,5,0,255);
    ht_limb(ht_raw,x+7,0,x-34,67,7,0,255);
    /* Goal: a ruined doorway with an open white passage. */
    x=HT_GOAL-camera;
    ht_rect(ht_raw,x-19,151,7,49,255); ht_rect(ht_raw,x+12,151,7,49,255);
    ht_rect(ht_raw,x-23,146,46,8,255);
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
static void ht_render_scene(void) {
    /* Input checkpoints may advance live physics. Freeze the rendered frame
     * so trees, focus and character all use one consistent game snapshot. */
    const ht_game game=ht;
    int camera=game.camera/256,px=game.x/256-camera,py=game.y/256-14;
    ht_clear_layer(ht_scene);
    for(int depth=0;depth<3;++depth) {
        int key=depth==0?camera*22/100:depth==1?camera*53/100:camera;
        if(!ht_cache_valid[depth] || ht_cache_key[depth]!=key) {
            ht_clear_layer(ht_raw);
            if(depth<2) ht_background(depth,camera); else ht_foreground(camera);
        }
        ht_checkpoint(); ht_prepare_layer(depth,key); ht_composite(depth,px,py);
    }
    ht_character(px,game.y/256,&game);
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
static void ht_pack(uint8_t *out,int stride) {
    static const uint8_t rank[8][8]={
        {41,8,52,7,45,15,47,4},{18,35,27,60,28,55,19,57},
        {54,1,49,9,62,3,40,14},{16,56,21,53,20,38,25,43},
        {59,12,42,6,39,10,34,5},{24,33,17,46,31,50,29,61},
        {36,2,58,11,63,0,51,13},{26,44,30,37,22,32,23,48}
    };
    /* Two logical samples produce four physical columns in both rows.
     * Reuse bilinear samples and remove per-pixel parity/edge branches. */
    for(int y=0;y<HT_H;++y) {
        const uint8_t *row=ht_scene+y*HT_W;
        const uint8_t *next=ht_scene+ht_min(HT_H-1,y+1)*HT_W;
        uint8_t *top=out+y*2*stride,*bottom=top+stride;
        const uint8_t *rt=rank[(y*2)&7],*rb=rank[(y*2+1)&7];
        for(int x=0;x<HT_W;x+=2) {
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
