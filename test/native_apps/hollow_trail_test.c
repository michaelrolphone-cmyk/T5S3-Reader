/* Host gameplay and memory-safety smoke test; no firmware/hardware mock. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../Apps/hollow_trail_engine.inc"

static uint32_t checksum(const uint8_t *data,size_t size) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<size;++i) h=(h^data[i])*16777619u;
    return h;
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY), *frame=malloc(960u*540u/4u);
    assert(memory && frame); ht_bind(memory); ht_spawn(true);
    /* Walk the entire route using the same fixed-step physics and hold jump
     * from each takeoff. A route that silently respawns cannot pass. */
    for(int tick=0;tick<2200 && !ht.laps;++tick) {
        bool jump=false;
        for(int i=0;i<HT_PLATFORMS-1;++i)
            if(ht.grounded && ht.x/256>=ht_land[i].right-8 && ht.x/256<=ht_land[i].right+4) jump=true;
        ht_step(1,jump,true);
    }
    assert(ht.laps==1 && ht.deaths==0);
    assert(ht.x==95*256 && ht.checkpoint==0);
    /* A failed jump returns to the latest checkpoint, not the start. */
    ht.checkpoint=6; ht.x=2180*256; ht.y=330*256; ht.vy=2400;
    ht_step(0,false,false);
    assert(ht.deaths==1 && ht.checkpoint==6);
    assert(ht.x==(ht_land[6].left+35)*256 && ht.grounded);
    /* Walk off without jumping: every pit must actually be lethal. */
    for(int i=0;i<HT_PLATFORMS-1;++i) {
        ht_spawn(true); ht.x=(ht_land[i].right-6)*256;
        ht.y=ht_land[i].top*256; ht.vx=640;
        unsigned deaths=ht.deaths;
        for(int t=0;t<100 && ht.deaths==deaths;++t) ht_step(1,false,false);
        assert(ht.deaths==deaths+1);
    }
    /* Render representative camera positions twice: deterministic, bounds
     * checked under ASan/UBSan, all four native tones represented. */
    const int positions[]={95,330,950,1660,2310,3130};
    clock_t start=clock();
    for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);++i) {
        ht_spawn(true); ht.x=positions[i]*256;
        ht.camera=ht_clamp(positions[i]-165,0,HT_GOAL-340)*256;
        ht_render_scene(); ht_pack(frame,240);
        uint32_t first=checksum(frame,960u*540u/4u);
        ht_render_scene(); ht_pack(frame,240);
        assert(first==checksum(frame,960u*540u/4u));
        unsigned tones=0;
        for(size_t n=0;n<960u*540u/4u;++n) for(int shift=0;shift<8;shift+=2)
            tones|=1u<<((frame[n]>>shift)&3);
        assert(tones==15);
    }
    printf("Hollow Trail: complete route, loop, checkpoints, all pits, deterministic 2bpp frames PASS (%.1f ms/host frame)\n",
           (double)(clock()-start)*1000.0/CLOCKS_PER_SEC/12.0);
    free(frame); free(memory); return 0;
}
