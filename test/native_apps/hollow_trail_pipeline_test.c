/* Exercise app_main with a delayed display, not a separate queue model. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
static uint32_t now_ms,release_at;
static unsigned allocations,frees,submissions,backbuffer_calls,strip_requests;
static bool armed,overlapped,reject_once,failed_alloc,transition;
static unsigned transition_loading_polls,reader_releases;
static bool journal_run,old_video,door_run;
static unsigned transition_level;
static uint8_t *stage;
static void *owned[HT_MEM_BLOCKS];
static uint8_t display[HT_PACKED_BYTES];
static uint32_t clock_ms(void) { return ++now_ms; }
static bool ready(void) {
    if(submissions==1 && !loading && !armed) {
        armed=true; release_at=now_ms+200;
        memset(display,0x5a,sizeof(display));
    }
    return !armed || now_ms>=release_at;
}
static bool poll_input(t5_app_input_t *out,uint32_t wait) {
    now_ms+=wait;
    if(loading && ht.level==transition_level) { ++transition_loading_polls; assert(ht.x==95*256); }
    assert(now_ms<20000);
    *out=(t5_app_input_t){.buttons=T5_APP_BUTTON_RIGHT,.exit_requested=submissions>=(journal_run || door_run?4u:3u)};
    if(stage && armed && now_ms<release_at && stage[0]!=0x5a) {
        overlapped=true;
        // No writes to queued display memory while staging the next frame.
        for(size_t i=0;i<sizeof(display);++i) assert(display[i]==0x5a);
    }
    return true;
}
static void *allocate(size_t size) {
    const size_t sizes[HT_MEM_BLOCKS]={HT_MEMORY,HT_NATIVE_PIXELS,HT_NATIVE_PIXELS,
                                     HT_PACKED_BYTES,HT_PACKED_BYTES};
    const unsigned slot=allocations++;
    assert(slot<HT_MEM_BLOCKS && size==sizes[slot]+15u);
    assert(!owned[slot]);
    /* The optional staging buffer is last, not the second allocation now
     * occupied by native raster A. Keep the direct-packing failure scenario. */
    if(slot==HT_MEM_STAGING && failed_alloc) return NULL;
    uint8_t *p=malloc(size);assert(p);memset(p,0x5a,size);
    owned[slot]=p;
    if(slot==HT_MEM_STAGING)
        stage=(uint8_t *)(((uintptr_t)p+15u)&~(uintptr_t)15u);
    return p;
}
static void release(void *p) {
    assert(p);
    unsigned slot=0;
    while(slot<HT_MEM_BLOCKS && owned[slot]!=p) ++slot;
    assert(slot<HT_MEM_BLOCKS); // Reject foreign pointers and double frees.
    owned[slot]=NULL;
    if(slot==HT_MEM_STAGING) stage=NULL;
    ++frees;free(p);
}
static bool start_video(t5_video_surface_v1 *s,uint8_t format) {
    assert(format==T5_VIDEO_PIXEL_MONO_1BPP_MSB);
    *s=(t5_video_surface_v1){960,540,120,format,T5_VIDEO_FLAG_ONE_IS_BLACK};
    return true;
}
static uint8_t *buffer(size_t *size) {
    assert(ready());++backbuffer_calls;*size=sizeof(display);return display;
}
static bool submit_frame(uint16_t y,uint16_t height) {
    assert(ready());
    if(!submissions || ((journal_run || door_run) && submissions>=2)) assert(y==0 && height==0);
    else assert(y==ht_dirty_top && height==ht_dirty_height);
    if(submissions==1 && stage) assert(!memcmp(display,stage,sizeof(display)));
    if(submissions==1 && reject_once) { reject_once=false; return false; }
    if(journal_run && submissions==2) {
        for(unsigned row=42;row<498;++row)
            for(unsigned col=17;col<113;++col) assert(display[row*120+col]==0xa5);
        assert(display[0]==255); // The notebook frame surrounds the typeset page.
    }
    if(journal_run && submissions==3) assert(display[0]!=0xa5);
    ++submissions;
    /* This fixture tests delayed-display ownership and live chapter changes.
     * The intro timeline has dedicated coverage; leave it after the startup
     * title frame so the historical submission counts remain about gameplay. */
    if(submissions==1 && ht_cutscene.active) {
        ht_cutscene.tick=HT_INTRO_TICKS;ht_cutscene.active=false;ht_cutscene.finished=true;
        ht_cutscene_apply_handoff(&ht_cutscene);ht_input_rearm=false;++scene_revision;
    }
    if(journal_run && submissions==2) { reading=true; ht_journal_open(30); ++scene_revision; }
    if(journal_run && submissions==3) { reading=false; ++scene_revision; }
    if(door_run && submissions==2) {
        ht.level=HT_LEVELS-1;ht_select_level(ht.level);ht_spawn(true);
        ht.x=HT_GOAL*256;ht.grounded=true;ht.verdict=1;ht.verdict_read=true;
        ht.puzzle.solved=true;ht.puzzle.opening=48;
        assert(ht_door_begin());++scene_revision;
    }
    if(door_run && submissions==3) {
        assert(ht.door_stage==HT_DOOR_YARD && !ht_framed);
        ht.level=0;ht_spawn(true);debug_jump=true;++scene_revision;
    }
    if(door_run && submissions==4)assert(ht.level==0 && !ht.door_stage && ht_framed);

    if(transition && submissions==2) {
        ht.level=transition_level-1;
        ht.x=HT_GOAL*256; ht.y=ht_land[9].top*256;
        ht.vx=ht.vy=0; ht.grounded=true; ht.puzzle.solved=true; ht.puzzle.opening=48; ht_step(0,false,false);
        assert(ht.level==transition_level && ht.checkpoint==0);
    }
    if(transition && submissions==3) {
        assert(ht_geometry_level==transition_level);
        if(transition_level==1) {
            /* Opaque composed city needs no obsolete skyline warmup. */
            assert(!transition_loading_polls && !ht_cache_builds);
            assert(ht_forest_frame_valid && ht_composed_frame_level==1);
        } else assert(transition_loading_polls>0); /* Other chapters still freeze during warmup. */
    }
    return true;
}
static bool reinforce(uint16_t y,uint16_t height,uint8_t passes) {
    assert(y==460 && height==46 && (passes==0 || passes==2));
    ++strip_requests;return true;
}
static uint32_t scans(void) { return now_ms/42; }
static void stop(void) {}
static const t5_app_api_v1 mock_app={.abi_version=T5_APP_ABI_VERSION,.struct_size=sizeof(mock_app),
    .poll=poll_input,.millis=clock_ms,.psram_alloc=allocate,.psram_free=release};
static const t5_video_api_v1 mock_video={.api_version=T5_VIDEO_API_VERSION,.struct_size=sizeof(mock_video),
    .start_format=start_video,.backbuffer=buffer,.can_submit=ready,.submit=submit_frame,
    .stop=stop,.frame_counter=scans,.reinforce_black=reinforce};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { (void)v;return &mock_app; }
const t5_video_api_v1 *t5_video_get_api(uint32_t v) {
    (void)v;static t5_video_api_v1 legacy;
    if(!old_video)return &mock_video;
    legacy=mock_video;legacy.struct_size=offsetof(t5_video_api_v1,reinforce_black);
    legacy.reinforce_black=NULL;return &legacy;
}
const t5_math_api_v1 *t5_math_get_api(uint32_t v) { (void)v;return NULL; }
static bool reader_page(void *context,const risc_reader_page_request_v1*q,risc_reader_page_result_v1*r) {
    assert(context==(void*)1 && q->width==768 && q->height==456 && q->stride==96);
    assert(q->offset<q->length); memset(q->pixels,0xa5,q->capacity); r->next_offset=q->length; return true;
}
static const risc_reader_typography_v1 reader_api={1,sizeof(reader_api),(void*)1,reader_page};
static bool capability_acquire(const char *name,uint32_t version,t5_provider_capability_lease_t*token,const void**api) {
    assert(version==1); *token=0; *api=NULL;
    if(strcmp(name,"reader.typography")) return false;
    *token=12; *api=&reader_api; return true;
}
static bool capability_release(t5_provider_capability_lease_t token) { assert(token==12); ++reader_releases; return true; }
static const t5_provider_capability_api_v1 capability_api={1,sizeof(capability_api),capability_acquire,capability_release,NULL};
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v) { (void)v;return journal_run?&capability_api:NULL; }
int main(void) {
    for(unsigned scenario=0;scenario<8;++scenario) {
        old_video=scenario==5;strip_requests=0;now_ms=release_at=0;allocations=frees=submissions=backbuffer_calls=0;
        armed=overlapped=false;reject_once=true;failed_alloc=scenario==1 || scenario==7;stage=NULL;
        door_run=scenario>=6;
        transition=scenario==2 || scenario==4;transition_level=scenario==4?4:1;transition_loading_polls=0; journal_run=scenario==3; reader_releases=0;
        app_main();
        assert(submissions==(journal_run || door_run?4u:3u) && backbuffer_calls==(journal_run || door_run?5u:4u));
        assert(old_video?strip_requests==0:(strip_requests>=1 && strip_requests<=submissions-1));
        assert(reader_releases==(journal_run?1u:0u));
        assert(allocations==HT_MEM_BLOCKS);
        assert(frees==HT_MEM_BLOCKS-(failed_alloc?1u:0u));
        for(unsigned slot=0;slot<HT_MEM_BLOCKS;++slot) assert(!owned[slot]);
        assert(overlapped==!failed_alloc);
    }
    puts("Hollow Trail pipeline: pack while busy, buffer ownership, retry, direct fallback and cleanup PASS");
}
