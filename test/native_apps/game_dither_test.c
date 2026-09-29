/* Exercise the actual renderers and format negotiation, not a panel model. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../Apps/risc_strike.c"
#include "../../Apps/hollow_trail_engine.inc"
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { (void)v; return NULL; }
const t5_video_api_v1 *t5_video_get_api(uint32_t v) { (void)v; return NULL; }
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v) { (void)v; return NULL; }
const t5_math_api_v1 *t5_math_get_api(uint32_t v) { (void)v; return NULL; }
static unsigned stops;
static bool bad_surface, fail_start;
static bool start_video(t5_video_surface_v1 *s,uint8_t f) {
    *s=(t5_video_surface_v1){960,540,f==1?120:240,f,T5_VIDEO_FLAG_ONE_IS_BLACK};
    if(bad_surface) s->stride_bytes=1;
    return !fail_start;
}
static void stop_video(void) { ++stops; }
static const t5_video_api_v1 video={.start_format=start_video,.stop=stop_video};
int main(void) {
    /* Packed arithmetic must be independent across lanes, including carries
     * and thresholds exactly at the 127/128 boundary. */
    for(unsigned a=0;a<256;++a) for(unsigned b=0;b<256;++b) {
        uint32_t av=a|((255-a)<<8)|(b<<16)|((255-b)<<24);
        uint32_t bv=b|(a<<8)|((255-b)<<16)|((255-a)<<24);
        uint32_t actual=ht_average4(av,bv);
        for(unsigned k=0;k<4;++k) {
            unsigned va=(av>>(8*k))&255,vb=(bv>>(8*k))&255;
            assert(((actual>>(8*k))&255)==(va+vb)/2);
        }
    }
    for(unsigned v=0;v<256;++v) for(unsigned threshold=1;threshold<128;++threshold) {
        uint32_t values=v*0x01010101u,t=threshold*0x01010101u;
        assert(ht_dots4(values,t,false)==(v>=threshold?0xaau:0u));
        assert(ht_dots4(values,t,true)==(v>=threshold+128?0xaau:0u));
    }

    unsigned previous_density=0;
    for(unsigned value=0;value<256;++value) {
        unsigned count=0;
        for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
            bool bit=epd_dither_black(value,x,y);
            assert(bit==epd_dither_black(value,x+960,y+8));
            count+=bit;
        }
        assert(count>=previous_density); previous_density=count;
        assert(abs((int)(count*255)-(int)(value*64))<=255);
        if(value==0 || value==255) assert(count==(value?64u:0u));
    }
    uint8_t *mono=malloc(120u*540),*gray=malloc(240u*540),*memory=malloc(HT_MEMORY);
    assert(mono && gray && memory); ht_bind(memory); ht_spawn(true); ht_render_scene();
    /* This loop is the AI renderer's standard physical-scale/dither contract.
     * AI+960 residual reconstruction has separate coverage in hollow_trail_test.c. */
    ht_neural_960=false;
    for(unsigned pattern=0;pattern<2;++pattern) {
        if(pattern) {
            ht_framed=false;
            for(unsigned i=0;i<HT_PIXELS;++i) ht_scene[i]=(uint8_t)ht_hash(i);
        }
        memset(mono,0xa5,120u*540); ht_pack_format(mono,120,true);
        for(int y=0;y<540;++y) for(int x=0;x<960;++x) {
            int sx=x/2,sy=y/2,nx=ht_min(sx+1,HT_W-1),ny=ht_min(sy+1,HT_H-1);
            int top=ht_scene[sy*HT_W+sx],bottom=ht_scene[ny*HT_W+sx];
            if(x&1) { top=(top+ht_scene[sy*HT_W+nx])/2; bottom=(bottom+ht_scene[ny*HT_W+nx])/2; }
            int value=(y&1)?(top+bottom)/2:top;
            assert(((mono[y*120+x/8]>>(7-(x&7)))&1)==epd_dither_black(value,x,y));
        }
    }
    ht_neural_960=true;
        /* Both raster paths must represent the same final lighting/material tone. */
    fps_reset_game();
    for(unsigned scene=0;scene<3;++scene) {
        g_player_angle=(float)scene*1.1f;
        g_surface=(t5_video_surface_v1){960,540,240,2,T5_VIDEO_FLAG_ONE_IS_BLACK};
        memset(gray,0,240u*540); fps_draw_world(gray,0); fps_draw_controller_hud(gray,0);
        g_surface.stride_bytes=120; g_surface.pixel_format=1;
        memset(mono,0,120u*540); fps_draw_world(mono,0); fps_draw_controller_hud(mono,0);
        for(unsigned y=0;y<540;++y) for(unsigned x=0;x<960;++x) {
            unsigned tone=(gray[y*240+x/4]>>(6-2*(x&3)))&3;
            assert(((mono[y*120+x/8]>>(7-(x&7)))&1)==epd_dither_black(tone*85,x,y));
        }
    }
    g_video=&video;
    for(unsigned mode=0;mode<2;++mode) { g_mono=mode; assert(fps_start_video()); }
    bad_surface=true; assert(!fps_start_video() && stops==1);
    bad_surface=false; fail_start=true; assert(!fps_start_video() && stops==1);
    g_paused=true; g_state=FPS_STATE_PLAYING; g_mode_down=false;
    fps_controller_state_t controller={.connected=true};
    assert(fps_update_controller_input(0,0,&controller,1u<<1,0,0));
    assert(g_mode_down && g_paused);
    free(memory);free(gray);free(mono);
    puts("Game dithering: density/endpoints, physical phase, bilinear packing, three lit scenes, format validation and paused selection PASS");
}
