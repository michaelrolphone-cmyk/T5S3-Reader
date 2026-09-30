#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned checkpoints;
static void service(void){++checkpoints;}
static void pgm(const char *name,const uint8_t *pixels) {
    FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P5\n%d %d\n255\n",HT_W,HT_H);
    assert(fwrite(pixels,1,HT_PIXELS,f)==HT_PIXELS);fclose(f);
}
int main(int argc,char **argv) {
    (void)argv;
    uint8_t *memory=malloc(HT_MEMORY),*background=malloc(HT_PIXELS),*expected=malloc(HT_PIXELS);
    uint8_t *low=malloc(HT_SCENE_PIXELS),*source_expected=malloc(HT_PIXELS);
    assert(memory && background && expected && low && source_expected);ht_bind(memory);ht_service=service;
    for(unsigned i=0;i<HT_SCENE_PIXELS;++i)low[i]=(uint8_t)ht_hash(i);
    /* Independent per-pixel oracle: every destination whose four source taps
     * contain any foreground modification must equal the old camera exactly. */
    for(unsigned n=0;n<180;++n) {
        memcpy(ht_low_scene,low,HT_SCENE_PIXELS);ht_upscale_scene();memcpy(background,ht_scene,HT_PIXELS);
        memcpy(ht_temp,background,HT_PIXELS);uint8_t *output=ht_scene;ht_scene=ht_temp;
        ht_camera_track_begin();
        for(unsigned j=0;j<(n%9);++j) {
            int x=(int)(ht_hash(n*13+j)%560)-40,y=(int)(ht_hash(n*71+j)%350)-40;
            ht_rect(ht_scene,x,y,1+(int)(ht_hash(j+n)%90),1+(int)(ht_hash(j+n+80)%70),j&1?255:0);
        }
        if(n%17==0) {
            int ink=n%3==0?0:n%3==1?255:127;
            ht_rect(ht_scene,0,0,HT_W,HT_H,ink);
            ht_rect(ht_scene,HT_W/2+(int)(n%16),13,1,HT_H-26,255-ink);
            ht_rect(ht_scene,17,HT_H/2+(int)(n%16),HT_W-34,1,255-ink);
        }
        ht_camera_tracking=false;ht_scene=output;
        ht_game game={0};game.sway_phase=n*37;game.rotation_phase=(n*73)<<8;game.camera_mood=n%257;game.drop_zoom=n%257;
        int turn,scale;ht_camera_coefficients(&game,&turn,&scale);
        ht_camera_into(ht_temp,&game);memcpy(expected,ht_scene,HT_PIXELS);
        ht_camera_fills_into(ht_temp,&game);
        for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)
            for(int x=ht_visible_left[y];x<HT_W-ht_visible_left[y];++x)
                assert(ht_scene[y*HT_W+x]==expected[y*HT_W+x]);
        unsigned before=checkpoints;ht_camera_split_into(ht_temp,&game);assert(checkpoints>before);
        assert(ht_camera_low_samples+ht_camera_low_skipped==0 || ht_camera_low_samples+ht_camera_low_skipped==HT_SCENE_PIXELS);
        for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)for(int x=ht_visible_left[y];x<HT_W-ht_visible_left[y];++x) {
            int u=(HT_W/2)*4096+(x-HT_W/2)*scale+(y-HT_H/2)*turn;
            int v=(HT_H/2)*4096-(x-HT_W/2)*turn+(y-HT_H/2)*scale;
            int i=(v>>12)*HT_W+(u>>12);
            if(ht_temp[i]!=background[i] || ht_temp[i+1]!=background[i+1] ||
               ht_temp[i+HT_W]!=background[i+HT_W] || ht_temp[i+HT_W+1]!=background[i+HT_W+1])
                assert(ht_scene[y*HT_W+x]==expected[y*HT_W+x]);
        }
        /* Verify skipped low samples cannot affect any final unoverlaid pixel:
         * recompute low camera without culling and compare the final image. */
        memcpy(expected,ht_scene,HT_PIXELS);
        memcpy(ht_low_scene,low,HT_SCENE_PIXELS);
        ht_render_test=HT_TEST_EVERYTHING;ht_simd_stage_ready=HT_OPT_SIMD_ALL;
        ht_camera_split_into(ht_temp,&game);
        for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)
            for(int x=ht_visible_left[y];x<HT_W-ht_visible_left[y];++x)
                assert(ht_scene[y*HT_W+x]==expected[y*HT_W+x]);
        ht_render_test=HT_TEST_BASE;ht_simd_stage_ready=0;
        int16_t left[HT_H],right[HT_H];memcpy(left,ht_camera_dest_left,sizeof(left));memcpy(right,ht_camera_dest_right,sizeof(right));
        if(ht_camera_low_samples+ht_camera_low_skipped) {
            memcpy(ht_low_scene,low,HT_SCENE_PIXELS);
            for(int y=0;y<HT_H;++y){ht_camera_dest_left[y]=HT_W;ht_camera_dest_right[y]=-1;}
            ht_camera_low_into(ht_scene,turn,scale);memcpy(ht_low_scene,ht_scene,HT_SCENE_PIXELS);ht_upscale_scene();
            for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)for(int x=ht_visible_left[y];x<HT_W-ht_visible_left[y];++x)
                if(x<left[y] || x>right[y])assert(ht_scene[y*HT_W+x]==expected[y*HT_W+x]);
        }
    }
    /* Coarse terrain proof: draw over two opposite backgrounds. Every safe
     * block must be independent of what was underneath, including bridges,
     * vistas, cliff insets and finite terrain bottoms. */
    for(unsigned n=0;n<120;++n) {
        ht_bind(memory);ht.level=n%HT_LEVELS;ht_spawn(true);
        ht.camera=(int)(ht_hash(n)%2900)*256;ht.camera_y=(int)(ht_hash(n+39)%80)*256;
        ht.vista=(int)(ht_hash(n+21)%257);ht.traversal.bridge_open=n%33;
        ht_select_level(ht.level);ht_occlusion_build(&ht);ht_world_scale=256-ht.vista*5/16;
        for(unsigned pass=0;pass<2;++pass) {
            memset(ht_scene,pass?255:0,HT_PIXELS);
            for(int i=0;i<HT_PLATFORMS;++i)for(int part=0;part<3;++part) {
                int l,r,top;if(!ht_platform_piece(&ht,i,part,&l,&r,&top))break;
                ht_draw_land(&ht,i,part,l,r,top);
            }
            if(!pass)memcpy(expected,ht_scene,HT_PIXELS);
            else for(int y=0;y<HT_H;++y)for(int x=0;x<HT_W;++x)
                if(ht_background_hidden(x,y))assert(ht_scene[y*HT_W+x]==expected[y*HT_W+x]);
        }
        ht_occlusion_active=false;ht_world_scale=256;
    }
    uint64_t total_low=0,total_foreground=0,total_skip=0,total_fill=0,occ_blend=0,occ_nn=0;unsigned full=0;
    for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<3;++view) {
        ht_bind(memory);ht.level=level;ht_spawn(true);ht.camera=view*733*256;ht.x=(view*733+190)*256;
        ht.vista=view?256:0;ht.sway_phase=128+view*317;ht.rotation_phase=(128+view*219)<<8;ht.camera_mood=256;
        ht_game game=ht;ht_render_scene();memcpy(expected,ht_scene,HT_PIXELS);memcpy(source_expected,ht_temp,HT_PIXELS);memcpy(background,ht_scene,HT_PIXELS);
        if(argc>1 && view==1){char path[96];snprintf(path,sizeof(path),"/tmp/camera-%u-before.pgm",level);pgm(path,ht_scene);}
        ht_bind(memory);ht=game;ht_render_test=HT_TEST_LOW_CAMERA;ht_render_scene();
        assert(!ht_camera_tracking && !ht_occlusion_active && ht_world_scale==256);
        assert(!memcmp(source_expected,ht_temp,HT_PIXELS));
        total_low+=ht_camera_low_samples;total_foreground+=ht_camera_foreground_samples;total_skip+=ht_camera_low_skipped;
        assert(!ht_camera_fill_pixels && !ht_occlusion_composite_skips && !ht_occlusion_neural_skips);
        if(!ht_camera_low_samples)++full;
        if(ht_grotto_opaque(&game))assert(!memcmp(expected,ht_scene,HT_PIXELS));
        if(argc>1 && view==1){char path[96];snprintf(path,sizeof(path),"/tmp/camera-%u-after.pgm",level);pgm(path,ht_scene);}
        memcpy(expected,ht_scene,HT_PIXELS);
        ht_bind(memory);ht=game;ht_render_test=HT_TEST_LOW_CAMERA;ht_render_scene();assert(!memcmp(expected,ht_scene,HT_PIXELS));
        for(unsigned mode=HT_TEST_OCCLUSION;mode<=HT_TEST_FILL_CAMERA;++mode) {
            ht_bind(memory);ht=game;ht_render_test=mode;ht_render_scene();
            assert(!memcmp(background,ht_scene,HT_PIXELS));
            assert(!ht_camera_low_samples && !ht_camera_low_skipped);
            if(mode==HT_TEST_OCCLUSION) {
                assert(!ht_camera_fill_pixels);occ_blend+=ht_occlusion_composite_skips;occ_nn+=ht_occlusion_neural_skips;
            } else {
                assert(!ht_occlusion_composite_skips && !ht_occlusion_neural_skips);total_fill+=ht_camera_fill_pixels;
            }
        }
        /* No camera motion must retain the old image exactly. */
        game.sway_phase=game.drop_zoom=0;
        ht_bind(memory);ht=game;ht_render_scene();memcpy(expected,ht_scene,HT_PIXELS);
        ht_bind(memory);ht=game;ht_render_test=HT_TEST_LOW_CAMERA;ht_render_scene();assert(!memcmp(expected,ht_scene,HT_PIXELS));
    }
    printf("30 views: low samples=%llu foreground samples=%llu skipped low=%llu full/no-low=%u\n",(unsigned long long)total_low,(unsigned long long)total_foreground,(unsigned long long)total_skip,full);
    printf("fill pixels=%llu skipped composition=%llu skipped neural gates=%llu\n",(unsigned long long)total_fill,(unsigned long long)occ_blend,(unsigned long long)occ_nn);
    assert(total_fill && occ_blend && occ_nn);
    ht_service=NULL;free(source_expected);free(low);free(expected);free(background);free(memory);
    puts("Low background camera: conservative projection, foreground exactness, low-sample culling, all chapters and no-motion fallback PASS");
}
