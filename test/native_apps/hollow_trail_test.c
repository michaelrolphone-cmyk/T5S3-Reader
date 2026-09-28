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
    assert(((uintptr_t)ht_low_scene&15u)==0);
    /* Walk the entire route using the same fixed-step physics and hold jump
     * from each takeoff. A route that silently respawns cannot pass. */
    unsigned visited=1;
    for(int tick=0;tick<HT_LEVELS*2200 && !ht.laps;++tick) {
        bool jump=false;
        for(int i=0;i<HT_PLATFORMS-1;++i)
            if(ht.grounded && ht.x/256>=ht_land[i].right-8 && ht.x/256<=ht_land[i].right+4) jump=true;
        int direction=1;
        if(ht.x/256>=2820 && ht.grounded && !ht.puzzle.solved) {
            const ht_puzzle_definition *p=&ht_puzzles[ht.level];
            int station=0;
            if(p->kind==0) station=p->answer[ht.puzzle.progress];
            else while(station<2 && ht.puzzle.value[station]==p->answer[station]) ++station;
            int target=HT_PUZZLE_FIRST+station*HT_PUZZLE_SPACING;
            direction=ht.x/256<target?1:-1;
            if(ht_puzzle_near(&ht)==station) { assert(ht_interact()); direction=0; }
        }
        ht_step(direction,jump,true);
        visited|=1u<<ht.level;
    }
    assert(visited==(1u<<HT_LEVELS)-1 && ht.laps==1 && ht.deaths==0);
    assert(ht.level==0 && ht.story_x==0);
    assert(ht.x==95*256 && ht.checkpoint==0);
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level;
        /* A failed jump returns to the latest checkpoint, not the start. */
        ht.story_x=2700; ht.checkpoint=6; ht.x=2180*256; ht.y=330*256; ht.vy=2400;
        ht_step(0,false,false);
        assert(ht.level==level && ht.checkpoint==6 && ht.story_x==2700);
        assert(ht.x==(ht_land[6].left+35)*256 && ht.grounded);
        /* Walk off without jumping: every pit must actually be lethal. */
        for(int i=0;i<HT_PLATFORMS-1;++i) {
            ht_spawn(true); ht.x=(ht_land[i].right-6)*256;
            ht.y=ht_land[i].top*256; ht.vx=640;
            unsigned deaths=ht.deaths;
            for(int t=0;t<100 && ht.deaths==deaths;++t) ht_step(1,false,false);
            assert(ht.deaths==deaths+1);
        }
    }
    ht.level=0; ht_spawn(true);
    /* Explicit authored solutions, not answers read out of game definitions. */
    const unsigned solutions[HT_LEVELS][5]={
        {2,0,1,9,9},{0,2,9,9,9},{0,1,1,1,2},
        {1,9,9,9,9},{0,2,1,0,9},{0,0,0,1,2},
        {1,2,9,9,9},{0,0,1,2,2},{1,0,2,1,9},{0,0,0,9,9}
    };
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level; ht_spawn(true);
        assert(!ht_interact()); /* Too far away. */
        ht.x=HT_PUZZLE_FIRST*256; ht.grounded=false; assert(!ht_interact());
        ht.x=HT_GOAL*256; ht.y=ht_land[9].top*256; ht.vy=0;
        ht_step(1,false,false);
        assert(ht.level==level && ht.x==HT_PUZZLE_GATE*256 && !ht.puzzle.solved);
        for(unsigned k=0;k<5 && solutions[level][k]!=9;++k) {
            ht.x=(HT_PUZZLE_FIRST+(int)solutions[level][k]*HT_PUZZLE_SPACING)*256;
            ht.y=ht_land[9].top*256; ht.grounded=true; assert(ht_interact());
        }
        /* Three dial chapters need one final turn of the third drum. */
        if(level==2 || level==5 || level==7) assert(ht_interact());
        assert(ht.puzzle.solved);
        unsigned builds=ht_cache_builds;
        assert(!ht_interact() && ht_cache_builds==builds); /* Solved state latches. */
        ht.checkpoint=9; ht_puzzle_state saved=ht.puzzle; ht_spawn(false);
        assert(!memcmp(&saved,&ht.puzzle,sizeof(saved)) && ht.checkpoint==9);
        ht.x=HT_GOAL*256; ht.y=ht_land[9].top*256; ht.vy=0; ht_step(1,false,false);
        assert(ht.level==(level+1)%HT_LEVELS && !ht.puzzle.solved && ht.puzzle.progress==0);
    }
    ht.level=0; ht_spawn(true); ht.grounded=true;
    ht.x=HT_PUZZLE_FIRST*256; assert(ht_interact());
    assert(ht.puzzle.wrong && ht.puzzle.progress==0);
    ht.x=(HT_PUZZLE_FIRST+2*HT_PUZZLE_SPACING)*256; assert(ht_interact());
    assert(!ht.puzzle.wrong && ht.puzzle.progress==1);
    ht.checkpoint=9; ht_spawn(false); assert(ht.puzzle.progress==1);
    ht.level=0; ht_spawn(true);
    for(unsigned level=0;level<HT_LEVELS;++level) {
        assert(strlen(ht_puzzles[level].name)*6<=330);
        for(int line=0;line<2;++line) {
            assert(strlen(ht_puzzles[level].clue[line])*6<=366);
            assert(strlen(ht_puzzles[level].reveal[line])*6<=366);
        }
    }
    /* Discovery requires proximity and grounded feet; records survive death
     * and chapter changes without revealing missing entries. */
    ht.evidence=0; memset(ht_scene,255,HT_PIXELS); ht_draw_journal(&ht,1);
    memcpy(frame,ht_scene,HT_PIXELS);
    ht.evidence=1; memset(ht_scene,255,HT_PIXELS); ht_draw_journal(&ht,1);
    assert(!memcmp(frame,ht_scene,HT_PIXELS)); /* Another find cannot reveal this page. */
    ht.evidence=2; memset(ht_scene,255,HT_PIXELS); ht_draw_journal(&ht,1);
    assert(memcmp(frame,ht_scene,HT_PIXELS));
    ht.evidence=0;
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level; ht_spawn(true); assert(ht_inspect()==-1);
        for(int item=0;item<3;++item) {
            unsigned page=level*3u+(unsigned)item;
            assert(!ht_evidence_found(&ht,page));
            ht.x=ht_evidence_x(level,item)*256;
            ht.y=ht_land[ht_evidence_platform(item)].top*256;
            ht.grounded=false; assert(ht_inspect()==-1);
            ht.grounded=true; assert(ht_inspect()==(int)page);
            assert(ht_evidence_found(&ht,page));
            uint32_t found=ht.evidence;
            assert(ht_inspect()==(int)page && ht.evidence==found);
            const ht_evidence_record *r=&ht_evidence[level][item];
            assert(strlen(r->title)*6<348);
            for(int line=0;line<4;++line) assert(strlen(r->lines[line])*6<=348);
            ht_spawn(false); assert(ht.evidence==found);
        }
    }
    assert(ht.evidence==((1u<<30)-1));
    ht_spawn(true); assert(ht.evidence==((1u<<30)-1));
    ht.level=0; ht_spawn(true);
    /* Narration milestones, glyph bounds and dirty-row coverage. */
    assert(ht_story_beat(0)==0 && ht_story_beat(1099)==0);
    assert(ht_story_beat(1100)==1 && ht_story_beat(2599)==1 && ht_story_beat(2600)==2);
    for(unsigned level=0;level<HT_LEVELS;++level) for(unsigned beat=0;beat<3;++beat) {
        ht.level=level; ht.story_x=beat==2?2600:beat==1?1100:0;
        assert(strlen(ht_chapters[level].title)*12<=249);
        for(unsigned line=0;line<2;++line) {
            const char *t=ht_chapters[level].lines[beat][line];
            assert(*t && strlen(t)*6<=366);
            for(;*t;++t) assert((*t>='A' && *t<='Z') || *t==' ' || *t=='.');
        }
        memset(ht_scene,255,HT_PIXELS); ht_narration(&ht);
        unsigned letters=0;
        for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) if(!ht_scene[y*HT_W+x]) {
            ++letters; assert(y>=233 && y<=249 && x>=57 && x<423);
        }
        assert(letters>0); ht_framed=true; ht_pack_mono(frame,120);
        for(unsigned y=0;y<540;++y) if(y<ht_dirty_top || y>=ht_dirty_top+ht_dirty_height)
            for(int x=0;x<120;++x) assert(frame[y*120+x]==255);
    }
    ht.level=0; ht_spawn(true);
    /* Render representative camera positions twice: deterministic, bounds
     * checked under ASan/UBSan, all four native tones represented. */
    const int positions[]={95,330,950,1660,2310,3130};
    /* Exhaustive normalization/quantization checks retain original arithmetic. */
    for(unsigned n=1;n<=19;++n) for(unsigned sum=0;sum<=255*n;++sum)
        assert(ht_average((int)sum,(0x1000000u+n-1u)/n)==sum/n);
    for(int value=0;value<256;++value) for(int rank=0;rank<64;++rank) {
        int q=value/85,rem=value-q*85;
        if(q<3 && rem*64>rank*85+42) ++q;
        assert(ht_quantize(value,rank)==q);
    }
    /* Cached shifts must equal a full fresh convolution, including reversals,
     * culling boundaries, both blur footprints, and large camera teleports. */
    uint8_t *expected=malloc(HT_PIXELS); assert(expected);
    for(int i=0;i<100;++i) {
        int camera=i<40?i*7:i<80?(79-i)*7:(i*137)%2860;
        ht.camera=camera*256; ht.x=(camera+165)*256;
        ht.y=(170+i%40)*256;
        ht_render_scene(); memcpy(expected,ht_scene,HT_PIXELS);
        memset(ht_cache_valid,0,sizeof(ht_cache_valid)); ht_render_scene();
        assert(!memcmp(expected,ht_scene,HT_PIXELS));
    }
    /* Cropped foreground convolution must retain the full filter's pixels. */
    for(int i=0;i<HT_PIXELS;++i) ht_raw[i]=(uint8_t)ht_hash((uint32_t)i);
    ht_blur_region(ht_raw,expected,4,0,HT_W);
    ht_blur_rect(ht_raw,ht_wide,4,HT_BORDER,HT_W-HT_BORDER,HT_BORDER,HT_H-HT_BORDER);
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)
        assert(!memcmp(expected+y*HT_W+HT_BORDER,ht_wide+y*HT_W+HT_BORDER,HT_W-2*HT_BORDER));
    /* Upscaling retains exact samples, clamps last rows/columns, and cannot
     * overflow on white/black transitions. Reference is per output pixel. */
    for(int i=0;i<HT_SCENE_PIXELS;++i) ht_low_scene[i]=(uint8_t)ht_hash((uint32_t)i);
    ht_upscale_scene();
    for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) {
        int sx=x/2,sy=y/2,nx=ht_min(sx+1,HT_SCENE_W-1),ny=ht_min(sy+1,HT_SCENE_H-1);
        int a=ht_low_scene[sy*HT_SCENE_W+sx],b=ht_low_scene[ny*HT_SCENE_W+sx];
        if(x%2) { a=(a+ht_low_scene[sy*HT_SCENE_W+nx])/2; b=(b+ht_low_scene[ny*HT_SCENE_W+nx])/2; }
        assert(ht_scene[y*HT_W+x]==(y%2?(a+b)/2:a));
    }
    /* Border and fade are symmetric and leave the central scene unchanged. */
    memset(ht_scene,100,HT_PIXELS); ht_vignette();
    for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) {
        int ex=ht_min(x,HT_W-1-x),ey=ht_min(y,HT_H-1-y),edge=ht_min(ex,ey);
        if(ex<HT_MASK_RADIUS && ey<HT_MASK_RADIUS) {
            int dx=HT_MASK_RADIUS-ex,dy=HT_MASK_RADIUS-ey,d=dx*dx+dy*dy,r=0;
            while((r+1)*(r+1)<=d) ++r;
            edge=HT_MASK_RADIUS-r;
        }
        int alpha=ht_clamp(edge-HT_BORDER,0,HT_FADE-HT_BORDER)*255/(HT_FADE-HT_BORDER);
        int wanted=255-(155*alpha+127)/255;
        assert(ht_scene[y*HT_W+x]==wanted);
    }
    /* Fast packing of hidden pixels equals the generic packer, in both modes. */
    uint8_t *generic=malloc(960u*540u/4u); assert(generic);
    for(int mono=0;mono<2;++mono) {
        int stride=mono?120:240;
        ht_framed=true; ht_pack_format(frame,stride,mono);
        ht_framed=false; ht_pack_format(generic,stride,mono);
        assert(!memcmp(frame,generic,(size_t)stride*540u));
    }
    free(generic);
    free(expected);
    clock_t start=clock();
    for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);++i) {
        ht_spawn(true); ht.x=positions[i]*256;
        ht.camera=ht_clamp(positions[i]-165,0,HT_GOAL-340)*256;
        ht_render_scene();
        ht_pack_mono(frame,120);
        assert(ht_dirty_top>0 && ht_dirty_height>0 && ht_dirty_top+ht_dirty_height<540);
        for(unsigned y=0;y<540;++y) if(y<ht_dirty_top || y>=ht_dirty_top+ht_dirty_height)
            for(int x=0;x<120;++x) assert(frame[y*120+x]==255);
        ht_pack(frame,240);
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
