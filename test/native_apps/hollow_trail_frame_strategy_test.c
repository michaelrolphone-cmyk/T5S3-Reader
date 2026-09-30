#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned checkpoints;
static void service(void){++checkpoints;}
static void scene(unsigned level,int view) {
    ht.level=level;ht_spawn(true);ht.camera=view*733*256;ht.x=(view*733+190)*256;
    ht.vista=view?256:0;ht.sway_phase=128+view*317;ht.rotation_phase=(128+view*219)<<8;ht.camera_mood=256;
}
/* Reference cheap frame draws current interactive content over retained scenery. */
static void cheap_reference(const uint8_t *background,const ht_game *g) {
    memcpy(ht_scene,background,HT_PIXELS);ht_world_scale=256-g->vista*5/16;
    int x=g->x/256-g->camera/256,feet=(g->y-g->camera_y)/256;
    ht_draw_traversal(g);ht_draw_puzzle(g);ht_character(x,feet,g);ht_weather(g);
    ht_world_scale=256;
    if(g->sway_phase || g->drop_zoom){memcpy(ht_temp,ht_scene,HT_PIXELS);ht_camera_into(ht_temp,g);}
    ht_vignette();
}
int main(void) {
    uint8_t *mem=malloc(HT_MEMORY),*bg=malloc(HT_PIXELS),*saved=malloc(HT_PIXELS),*want=malloc(HT_PIXELS);
    assert(mem&&bg&&saved&&want);
    const unsigned masks[]={0,1,2,4,7};
    ht_bind(mem);assert(ht_frame_mode==HT_FRAME_BASE);
    for(unsigned m=0;m<HT_FRAME_COUNT;++m){ht_frame_select(m);assert(ht_frame_flags()==masks[m]);assert(!ht_background_valid&&!ht_motion_valid);}
    ht_frame_select(HT_FRAME_COUNT);assert(ht_frame_mode==HT_FRAME_BASE);
    for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<3;++view) {
        ht_bind(mem);scene(level,view);ht_game original=ht;ht_render_scene();memcpy(want,ht_scene,HT_PIXELS);
        ht_bind(mem);ht=original;ht_retained_background=bg;ht_frame_select(HT_FRAME_ALTERNATE);ht_service=service;
        ht_render_scene();assert(!ht_frame_reused&&ht_background_builds==1);assert(!memcmp(want,ht_scene,HT_PIXELS));
        memcpy(saved,bg,HT_PIXELS);unsigned builds=ht_cache_builds;
        ht.x+=3*256;ht.camera+=2*256;ht.ticks+=3;ht_game next=ht;
        ht_render_scene();assert(ht_frame_reused&&ht_background_builds==1&&ht_background_reuses==1);
        assert(builds==ht_cache_builds&&!memcmp(saved,bg,HT_PIXELS));memcpy(want,ht_scene,HT_PIXELS);
        cheap_reference(saved,&next);assert(!memcmp(want,ht_scene,HT_PIXELS));
        ht_render_scene();assert(!ht_frame_reused&&ht_background_builds==2);
        ht_render_scene();assert(ht_frame_reused&&ht_background_reuses==2);
        ht_frame_invalidate();ht_render_scene();assert(!ht_frame_reused);
        ++ht.deaths;ht_render_scene();assert(!ht_frame_reused);
        ht.camera+=100*256;ht_render_scene();assert(!ht_frame_reused);
        ht_frame_select(HT_FRAME_ALTERNATE);ht_retained_background=NULL;
        ht_render_scene();ht_render_scene();assert(!ht_frame_reused&&ht_background_builds==2);
        /* No movement: the foveated test restores the exact baseline. */
        ht_bind(mem);ht=original;ht_frame_select(HT_FRAME_FOVEATED);ht.vx=ht.vy=0;
        ht_render_scene();assert(!ht_frame_low_active);memcpy(want,ht_scene,HT_PIXELS);
        ht_frame_select(HT_FRAME_BASE);ht_render_scene();assert(!memcmp(want,ht_scene,HT_PIXELS));
        ht_frame_select(HT_FRAME_FOVEATED);ht.vx=256;ht_render_scene();assert(ht_frame_low_active&&ht_camera_coarse_blocks);
        ht.vx=0;ht_render_scene();assert(!ht_frame_low_active);
        ht_frame_select(HT_FRAME_ALL);ht_retained_background=bg;ht.vx=256;
        ht_render_scene();assert(!ht_frame_reused&&ht_frame_low_active&&ht_camera_coarse_blocks);
        ht.x+=256;ht_render_scene();assert(ht_frame_reused&&ht_frame_low_active&&ht_camera_coarse_blocks);
    }
    /* Synthetic camera: rounded nearest samples and exact full-detail zone,
     * including offscreen characters, identity, both rotation signs and zoom. */
    ht_bind(mem);ht_service=service;
    for(unsigned i=0;i<HT_PIXELS;++i)ht_temp[i]=(uint8_t)ht_hash(i);
    for(unsigned n=0;n<96;++n) {
        ht_game g={0};g.sway_phase=n?n*17:0;g.rotation_phase=n*73*256;g.camera_mood=n%257;g.drop_zoom=n%257;
        int turn=0,scale=4096;if(g.sway_phase||g.drop_zoom)ht_camera_coefficients(&g,&turn,&scale);
        int cx=(int)(ht_hash(n)%620)-70,cy=(int)(ht_hash(n+97)%410)-70;
        for(unsigned nearest=0;nearest<2;++nearest) {
            memset(ht_scene,0x5a,HT_PIXELS);ht_camera_strategy_into(ht_temp,&g,cx,cy,false,nearest);
            memcpy(want,ht_scene,HT_PIXELS);
            for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)for(int x=ht_visible_left[y];x<HT_W-ht_visible_left[y];++x) {
                int u=(HT_W/2)*4096+(x-HT_W/2)*scale+(y-HT_H/2)*turn;
                int v=(HT_H/2)*4096-(x-HT_W/2)*turn+(y-HT_H/2)*scale;
                if(nearest)assert(want[y*HT_W+x]==ht_temp[((v+2048)>>12)*HT_W+((u+2048)>>12)]);
                else {
                    const uint8_t *a=ht_temp+(v>>12)*HT_W+(u>>12),*b=a+HT_W;
                    int fx=(u>>8)&15,fy=(v>>8)&15;
                    int top=a[0]*16+((int)a[1]-a[0])*fx,bottom=b[0]*16+((int)b[1]-b[0])*fx;
                    assert(want[y*HT_W+x]==((top*16+(bottom-top)*fy)>>8));
                }
            }
            memset(ht_scene,0x5a,HT_PIXELS);ht_camera_full_samples=ht_camera_coarse_blocks=0;
            ht_camera_strategy_into(ht_temp,&g,cx,cy,true,nearest);assert(ht_camera_coarse_blocks);
            for(int y=0;y<HT_H;++y)for(int x=0;x<HT_W;++x) {
                if(y<HT_BORDER||y>=HT_H-HT_BORDER||x<ht_visible_left[y]||x>=HT_W-ht_visible_left[y]){assert(ht_scene[y*HT_W+x]==0x5a);continue;}
                int u=(HT_W/2)*4096+(x-HT_W/2)*scale+(y-HT_H/2)*turn;
                int v=(HT_H/2)*4096-(x-HT_W/2)*turn+(y-HT_H/2)*scale;
                if(ht_abs((u>>12)-cx)<=HT_DETAIL_HALF_W && ht_abs((v>>12)-cy)<=HT_DETAIL_HALF_H)
                    assert(ht_scene[y*HT_W+x]==want[y*HT_W+x]);
            }
        }
    }
    assert(checkpoints);free(want);free(saved);free(bg);free(mem);
    puts("Frame strategies: alternation, fresh actors, invalidation/fallback, stationary restoration, nearest oracle, protected character region and combined mode PASS");
}
