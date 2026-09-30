/* Frozen pre-optimization raster is the pixel contract, including signed
 * truncation and the grotto exit crossfade. No quality/FPS approximations. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static void grotto_reference(const ht_game *g) {
    if(g->level!=4 || g->camera/256>900) return;
    int cx=g->camera/256,cy=g->camera_y/256;
    static int16_t near_roof[HT_W];
    for(int sx=0;sx<HT_W;++sx) {
        int source=240+(sx-240)*256/ht_world_scale;
        near_roof[sx]=ht_project_y(ht_grotto_ceiling(source+cx)-cy);
        ht_land_tops[sx]=(int16_t)ht_project_y(ht_cave_face(source+cx*53/100,0)-cy*53/100);
        ht_land_bottoms[sx]=(int16_t)ht_project_y(ht_cave_face(source+cx*22/100,1)-cy*22/100);
    }
    int water=ht_project_y(ht_mech(g)->boat_deck+10-cy);
    int glow=ht_project_x(535-cx*22/100),middle=ht_project_y(192-cy*22/100);
    int blend=ht_clamp((900-cx)*256/200,0,256);
    int softness=ht_max(1,8*ht_world_scale/256);
    uint16_t edge[24];
    for(int d=0;d<=softness;++d) edge[d]=(uint16_t)(d*256/softness);
    int boat=ht_project_x(g->traversal.boat_x/256-cx);
    for(int sy=0;sy<HT_H;++sy) {
        uint8_t *row=ht_scene+sy*HT_W;
        for(int sx=0;sx<HT_W;++sx) {
            int dx=sx-glow,dy=sy-middle;
            int ink=ht_clamp(18+(dx*dx+dy*dy*2)/1300,18,125);
            int far_distance=sy-ht_land_bottoms[sx];
            int far=far_distance<=0?256:edge[ht_clamp(softness-far_distance,0,softness)]/3;
            int mid_distance=sy-ht_land_tops[sx];
            int mid=mid_distance<=0?256:edge[ht_clamp(softness-mid_distance,0,softness)]/3;
            int far_ink=145-ht_clamp(sy-50,0,180)/2;
            ink=ht_max(ink,ink+(far_ink-ink)*far/256);
            int mid_ink=222-ht_clamp(sy-80,0,160)/3;
            ink=ht_max(ink,ink+(mid_ink-ink)*mid/256);
            if(sy>=water) {
                int depth=sy-water;
                /* The water is its own horizontal surface, not cave fog.
                 * A dark plane, reflected hull and sparse ripples retain a
                 * visible waterline even beneath the brightest opening. */
                ink=ht_clamp(110+depth/3+ht_abs(dx)/12,110,170);
                int reflection=ht_clamp((235-sx)*2,0,70);
                ink=ht_min(210,ink+reflection*(80-ht_min(depth,80))/80);
                int spread=ht_max(0,48-ht_abs(sx-boat));
                int shade=spread*ht_max(0,34-depth)/17;
                if((depth/3)%3==1) shade=shade*2/3;
                ink=ht_min(225,ink+shade);
                if(depth<=1) ink=ht_max(65,ink-32);
                int ripple=(sx+cx/2+depth*7)%83;
                if(depth>4 && depth%11==0 && ripple<17) ink=ht_max(70,ink-9);
            }
            int distance=sy-near_roof[sx];
            if(distance<=0) ink=255;
            else if(distance<softness) ink=ht_max(ink,150*edge[softness-distance]/256);
            row[sx]=(uint8_t)((ink*blend+row[sx]*(256-blend))/256);
        }
        if((sy&15)==15) ht_checkpoint();
    }
}

static uint32_t frame_hash(void) {
    uint32_t h=2166136261u;
    for(int i=0;i<HT_PIXELS;++i) h=(h^ht_scene[i])*16777619u;
    return h;
}
static unsigned calls;
static void service(void) {++calls;}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+32),*expected=malloc(HT_PIXELS);
    assert(memory && expected);memset(memory+HT_MEMORY,0x5a,32);ht_bind(memory);
    const int cameras[]={0,120,240,420,680,700,701,800,899,901};
    const int vertical[]={-80,0,40,180,360};
    ht.level=4;ht_spawn(true);
    for(unsigned c=0;c<sizeof(cameras)/sizeof(cameras[0]);++c)
        for(unsigned v=0;v<sizeof(vertical)/sizeof(vertical[0]);++v)
            for(int scale=176;scale<=256;scale+=40) for(int boat=398;boat<=632;boat+=234) {
                ht.camera=cameras[c]*256;ht.camera_y=vertical[v]*256;
                ht.traversal.boat_x=boat*256;ht_world_scale=scale;
                for(int i=0;i<HT_PIXELS;++i) ht_scene[i]=(uint8_t)(i*37+i/HT_W);
                grotto_reference(&ht);memcpy(expected,ht_scene,HT_PIXELS);
                for(int i=0;i<HT_PIXELS;++i) ht_scene[i]=(uint8_t)(i*37+i/HT_W);
                calls=0;ht_service=service;ht_boat_grotto(&ht);ht_service=NULL;
                assert(!memcmp(expected,ht_scene,HT_PIXELS));
                if(cameras[c]<900) assert(calls>0);
            }
    /* Original full-frame captures include traversal, water, hull, character,
     * camera transform and vignette. They detect over-aggressive occlusion. */
    /* 1.1.26 walking pose; the 300 independent grotto rasters above remain exact. */
    const uint32_t golden[]={0x7db2d696,0x35b0760b,0xefd71e48,0x29c2a794,0xd16ff6be,0x6ad390e1,0x7961e636,0xedc137c2,0xdf677af9,0xd21902dd,};
    for(unsigned n=0;n<sizeof(cameras)/sizeof(cameras[0]);++n) {
        ht.level=4;ht_spawn(true);ht.camera=cameras[n]*256;ht.camera_y=40*256;
        ht.x=(cameras[n]+190)*256;ht.vista=n%2?256:0;
        ht.sway_phase=512;ht.rotation_phase=256u<<8;
        ht.traversal.boat_x=(398+n*20)*256;ht.ticks=33+n*16;
        ht_render_scene();assert(frame_hash()==golden[n]);
        if(cameras[n]<=700) {
            unsigned builds=ht_cache_builds;
            ht_render_scene();assert(frame_hash()==golden[n]);
            assert(builds==ht_cache_builds); // No hidden cache work in the opaque frame.
        }
    }
    for(int i=0;i<32;++i) assert(memory[HT_MEMORY+i]==0x5a);
    free(expected);free(memory);
    puts("Hollow Trail grotto: 300 exact reference views, 10 nearest-camera frames, opacity/exit boundaries and checkpoints PASS");
}
