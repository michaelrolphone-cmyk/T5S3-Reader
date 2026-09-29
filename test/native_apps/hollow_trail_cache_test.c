/* World-strip cache invariants: compare with full viewport geometry and filters,
 * sampled onto the reduced scene grid, not another cache implementation. Also cover bounded speculative work. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"

static unsigned checkpoints;
static bool change_level;
static void service(void) {
    ++checkpoints;
    if(change_level) { change_level=false; ht.level=(ht.level+1)%HT_LEVELS; }
}
static void reference(void) {
    const ht_game game=ht;
    int camera=game.camera/256,px=game.x/256-camera,py=(game.y-game.camera_y)/256-14;
    const int radius[]={3,1,0,0},spread[]={9,6,4,4};
    memset(ht_scene,ht_sky_ink(&game),HT_PIXELS);
    for(int depth=0;depth<HT_LAYERS;++depth) if(depth!=2) for(int half=0;half<2;++half) {
        /* Two overlapping viewport renders provide independent horizontal
         * blur halos now that the visible border is thinner than the filter. */
        int shift=half?HT_W/4:-HT_W/4;
        ht_clear_layer(ht_raw);
        int offset=ht_layer_offset(depth,camera)+shift;
        if(depth<2) ht_background(depth,offset);
        else if(depth==2) ht_foreground(offset);
        else ht_branches(offset);
        ht_blur_region(ht_raw,ht_near,radius[depth],0,HT_W);
        ht_blur_region(ht_near,ht_wide,spread[depth],0,HT_W);
        /* Independent full-resolution lighting reference, sampled afterward. */
        for(int y=0;y<HT_H;++y) for(int x=half*HT_W/2;x<(half+1)*HT_W/2;++x) {
            int dx=x-px,dy=y-py,i=y*HT_W+x;
            int radial=ht_clamp((dx*dx+dy*dy-2500)/200,0,256);
            int src=y*HT_W+x-shift;
            int v=(ht_near[src]*(256-radial)+ht_wide[src]*radial)>>8;
            v=v*(256-((radial*(depth>=2?70:45))>>8))>>8;
            ht_scene[i]=(uint8_t)ht_max(ht_scene[i],v);
        }
    }
    uint8_t sampled[HT_SCENE_PIXELS];
    for(int y=0;y<HT_SCENE_H;++y) for(int x=0;x<HT_SCENE_W;++x)
        sampled[y*HT_SCENE_W+x]=ht_scene[(2*y)*HT_W+2*x];
    for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) {
        int sx=x/2,sy=y/2,nx=ht_min(sx+1,HT_SCENE_W-1),ny=ht_min(sy+1,HT_SCENE_H-1);
        int top=sampled[sy*HT_SCENE_W+sx],bottom=sampled[ny*HT_SCENE_W+sx];
        if(x&1) { top=(top+sampled[sy*HT_SCENE_W+nx])/2;bottom=(bottom+sampled[ny*HT_SCENE_W+nx])/2; }
        ht_scene[y*HT_W+x]=(uint8_t)((y&1)?(top+bottom)/2:top);
    }
    ht_draw_traversal(&game);
    ht_draw_evidence(&game);
    ht_draw_puzzle(&game);
    ht_character(px,(game.y-game.camera_y)/256,&game); ht_weather(&game); if(game.sway_phase || game.drop_zoom) {memcpy(ht_temp,ht_scene,HT_PIXELS);ht_camera_into(ht_temp,&game);} ht_vignette();
}
static unsigned dsp_calls;
static bool mock_mul(const int16_t *a,const int16_t *b,int16_t *out,size_t count) {
    assert(!(((uintptr_t)a|(uintptr_t)b|(uintptr_t)out)&15u));
    assert(count<=HT_DSP_BATCH); ++dsp_calls;
    for(size_t i=0;i<count;++i) {
        int product=a[i]*b[i]; assert(product>=-32768 && product<=32767);
        out[i]=(int16_t)product;
    }
    return true;
}
static const t5_math_api_v1 dsp_mock={.api_version=T5_MATH_API_VERSION,
    .struct_size=sizeof(t5_math_api_v1),.features=T5_MATH_FEATURE_S3_DSP,.mul_s16=mock_mul};
/* Every cactus remains one silhouette before and after 2x sampling, at
 * both camera parities. In particular the 39px cactus keeps a 2-sample stem. */
static void cactus_shapes(void) {
    static unsigned queue[HT_PIXELS];
    static uint8_t seen[HT_PIXELS];
    for(int height=39;height<=103;++height) {
        memset(ht_raw,0,HT_PIXELS); ht_cactus(240,220,height,255);
        for(int step=1;step<=2;++step) for(int ox=0;ox<step;++ox) for(int oy=0;oy<step;++oy) {
            int width=HT_W/step,rows=HT_H/step;
            unsigned total=0,first=0;
            memset(seen,0,sizeof(seen));
            for(int y=0;y<rows;++y) for(int x=0;x<width;++x)
                if(ht_raw[(y*step+oy)*HT_W+x*step+ox]) {first=y*width+x; ++total;}
            assert(total); unsigned head=0,tail=1;queue[0]=first;seen[first]=1;
            while(head<tail) {
                unsigned at=queue[head++]; int x=at%width,y=at/width;
                const int dx[]={-1,1,0,0},dy[]={0,0,-1,1};
                for(int k=0;k<4;++k) {
                    int nx=x+dx[k],ny=y+dy[k];
                    if(nx<0 || nx>=width || ny<0 || ny>=rows) continue;
                    unsigned next=ny*width+nx;
                    if(!seen[next] && ht_raw[(ny*step+oy)*HT_W+nx*step+ox]) {
                        seen[next]=1; queue[tail++]=next;
                    }
                }
            }
            assert(tail==total);
            if(height==39 && step==2) {
                unsigned stem=0;
                for(int x=0;x<width;++x) stem+=ht_raw[210*HT_W+x*2+ox]!=0;
                assert(stem==2);
            }
        }
    }
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+32),*expected=malloc(HT_PIXELS);
    assert(memory && expected); memset(memory+HT_MEMORY,0x5a,32);
    ht_bind(memory); cactus_shapes(); ht_spawn(true);ht_service=service;
    int steps=0; while(ht_cache_prefetch(0,1)) assert(++steps<400);
    assert(ht_cache_builds==9 && checkpoints>0);
    unsigned builds=ht_cache_builds;
    /* Moving within warmed strips must not regenerate ANY geometry or blur. */
    for(int camera=0;camera<120;camera+=3) {
        ht.camera=camera*256; ht.x=(camera+165)*256; ht_render_scene();
    }
    assert(ht_cache_builds==builds);
    /* Four bounded work slices per frame keep up with 12px camera steps
     * (roughly walking at the measured 6.6 FPS); all visible data is ready. */
    for(int camera=0;camera<2400;camera+=12) {
        for(int depth=0;depth<HT_LAYERS;++depth) {
            if(depth==2) continue;
            int offset=ht_layer_offset(depth,camera);
            int first=ht_floor_div(offset+HT_BORDER,HT_TILE_W);
            int last=ht_floor_div(offset+HT_W-HT_BORDER-1,HT_TILE_W);
            for(int key=first;key<=last;++key) assert(ht_cache_has(depth,key));
        }
        ht.camera=camera*256; ht.x=(camera+165)*256; ht_render_scene();
        for(int i=0;i<4;++i) if(!ht_cache_prefetch(camera,1)) break;
    }
    const int cameras[]={0,1,22,95,240,255,256,257,460,511,512,900,1711,2860,512,256,0};
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level; ht_select_level(level);
        for(unsigned n=0;n<sizeof(cameras)/sizeof(cameras[0]);++n) {
            int camera=cameras[n]; ht.camera=camera*256; ht.x=(camera+165)*256;
            /* Reference uses the work buffers, so cancel speculative work first. */
            ht_cache_job.active=false; reference(); memcpy(expected,ht_scene,HT_PIXELS);
            ht_render_scene();
            if(memcmp(expected,ht_scene,HT_PIXELS)) {
                for(int i=0;i<HT_PIXELS;++i) if(expected[i]!=ht_scene[i]) {
                    fprintf(stderr,"camera %d pixel %d,%d expected %d got %d\n",camera,i%HT_W,i/HT_W,expected[i],ht_scene[i]);break;
                }
                assert(0);
            }
            ht_math=&dsp_mock; ht_dsp_composite=true;
            ht_render_scene(); assert(!memcmp(expected,ht_scene,HT_PIXELS));
            assert(dsp_calls>0); ht_dsp_composite=false; ht_math=NULL;
            /* Partially prepare rightward work, then reverse; no stale strip use. */
            for(int i=0;i<8;++i) (void)ht_cache_prefetch(camera,1);
            for(int i=0;i<8;++i) (void)ht_cache_prefetch(camera,-1);
            ht_render_scene(); assert(!memcmp(expected,ht_scene,HT_PIXELS));
        }
    }
    ht.level=0; ht_spawn(true); ht_select_level(0);
    ht_render_scene(); memcpy(expected,ht_scene,HT_PIXELS);
    memset(ht_cache_valid,0,sizeof(ht_cache_valid)); change_level=true;
    ht_render_scene();
    assert(ht.level==1 && ht_geometry_level==0 && !memcmp(expected,ht_scene,HT_PIXELS));
    ht_render_scene();
    assert(ht_geometry_level==1 && memcmp(expected,ht_scene,HT_PIXELS));
    for(int i=0;i<32;++i) assert(memory[HT_MEMORY+i]==0x5a);
    ht_abort=true; assert(!ht_cache_prefetch(2000,1)); assert(!ht_cache_visible(2000));
    free(expected);free(memory);
    puts("Hollow Trail cache: warm reuse, sampled full-render equivalence, tile boundaries, reversals, teleports, bounds and cancellation PASS");
}
