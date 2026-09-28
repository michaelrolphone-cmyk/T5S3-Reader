#include "T5AppApi.h"
#include <stdio.h>
#include "T5VideoApi.h"
#include "T5HardwareTakeover.h"
#include "T5ProviderCapabilityApi.h"
#include "RiscUsbHidV1.h"
#include "RiscReaderTypographyV1.h"
#include "hollow_trail_engine.inc"

static const t5_app_api_v1 *app;
static const t5_provider_capability_api_v1 *caps;
static const risc_usb_gamepad_api_v1 *pad;
static t5_provider_capability_lease_t lease=T5_PROVIDER_CAPABILITY_LEASE_INVALID;
static bool quitting, jump_down, pause_down, paused, mode_down;
static uint32_t held, previous, last_poll;
static uint32_t simulation_clock, simulation_accumulator, scene_revision;
static bool simulation_started,loading,reading;
static unsigned journal_page;
#define HT_FRAME_INTERVAL_MS 42u /* At most 24 submissions/s; physics stays 32ms. */
static struct {
    uint32_t start,scan_start,frames,render_ms,pack_ms,wait_ms,cache_ms,copy_ms;
    uint32_t fps10,scan10,render_avg,pack_avg,wait_avg,cache_avg,copy_avg;
} ht_perf;
static void ht_perf_finish(const t5_video_api_v1 *video,uint32_t now) {
    ++ht_perf.frames;
    uint32_t elapsed=now-ht_perf.start;
    if(elapsed<1000u) return;
    uint32_t scans=video->struct_size>=offsetof(t5_video_api_v1,frame_counter)+sizeof(video->frame_counter) && video->frame_counter?video->frame_counter():0;
    ht_perf.fps10=ht_perf.frames*10000u/elapsed;
    ht_perf.scan10=(scans-ht_perf.scan_start)*10000u/elapsed;
    ht_perf.render_avg=ht_perf.render_ms/ht_perf.frames;
    ht_perf.pack_avg=ht_perf.pack_ms/ht_perf.frames;
    ht_perf.wait_avg=ht_perf.wait_ms/ht_perf.frames;
    ht_perf.cache_avg=ht_perf.cache_ms/ht_perf.frames;
    ht_perf.copy_avg=ht_perf.copy_ms/ht_perf.frames;
    ht_perf.start=now;ht_perf.scan_start=scans;
    ht_perf.frames=ht_perf.render_ms=ht_perf.pack_ms=ht_perf.wait_ms=ht_perf.cache_ms=ht_perf.copy_ms=0;
}
#define HT_LEFT 1u
#define HT_RIGHT 2u
#define HT_JUMP 4u
#define HT_INTERACT 32u
#define HT_PAUSE 8u
#define HT_EXIT 16u
#define HT_HAS(api,type,field) ((api) && (api)->struct_size >= offsetof(type,field)+sizeof((api)->field) && (api)->field)
#include "hollow_trail_journal.inc"
static void ht_log(const char *message) {
    if(HT_HAS(app,t5_app_api_v1,log_message)) app->log_message(message);
}
static void ht_release_pad(void) {
    if(lease!=T5_PROVIDER_CAPABILITY_LEASE_INVALID && caps) (void)caps->release(lease);
    lease=T5_PROVIDER_CAPABILITY_LEASE_INVALID; pad=NULL; caps=NULL;
}
static void ht_acquire_pad(void) {
    caps=t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if(!caps || caps->api_version!=T5_PROVIDER_CAPABILITY_API_VERSION ||
       !HT_HAS(caps,t5_provider_capability_api_v1,acquire) ||
       !HT_HAS(caps,t5_provider_capability_api_v1,release)) { caps=NULL; return; }
    const void *api=NULL;
    if(!caps->acquire("usb.xinput.gamepad",RISC_USB_GAMEPAD_API_V1,&lease,&api)) return;
    pad=(const risc_usb_gamepad_api_v1 *)api;
    if(!pad || pad->api_version!=RISC_USB_GAMEPAD_API_V1 ||
       !HT_HAS(pad,risc_usb_gamepad_api_v1,snapshot) || !pad->poll) ht_release_pad();
}
static void ht_advance(uint32_t now) {
    if(reading || loading || ht.level!=ht_geometry_level) { simulation_started=false; simulation_accumulator=0; jump_down=pause_down=false; return; }
    if(!simulation_started) { simulation_clock=now; simulation_started=true; }
    if(pause_down) {
        paused=!paused; pause_down=false; simulation_accumulator=0; ++scene_revision;
    }
    uint32_t elapsed=now-simulation_clock;
    simulation_clock=now;
    simulation_accumulator+=elapsed>128u?128u:elapsed;
    if(paused) { mode_down|=jump_down; simulation_accumulator=0; jump_down=false; return; }
    for(unsigned steps=0;simulation_accumulator>=HT_STEP_MS && steps<8;++steps) {
        int direction=((held&HT_RIGHT)!=0)-((held&HT_LEFT)!=0);
        ht_game before=ht;
        ht_step(direction,jump_down,(held&HT_JUMP)!=0);
        jump_down=false; simulation_accumulator-=HT_STEP_MS;
        if(before.x!=ht.x || before.y!=ht.y || before.camera!=ht.camera ||
           before.story_x!=ht.story_x || before.level!=ht.level || before.stride!=ht.stride || before.facing!=ht.facing || before.laps!=ht.laps)
            ++scene_revision;
        if(before.level!=ht.level) { simulation_accumulator=0; break; }
    }
}
static void ht_input(uint32_t wait) {
    t5_app_input_t in={0};
    if(!app->poll(&in,wait) || in.exit_requested) { quitting=true; return; }
    uint32_t buttons=0; bool connected=false;
    if(pad && pad->poll(pad->context,8u)) {
        risc_usb_gamepad_state_v1 states[4]; size_t count=4;
        memset(states,0,sizeof(states));
        if(pad->snapshot(pad->context,states,&count)) {
            if(count>4) count=4;
            for(size_t i=0;i<count;++i) if(states[i].connected) {
                connected=true;
                uint8_t h=states[i].hat;
                if(h==5 || h==6 || h==7 || states[i].x < -12000) buttons|=HT_LEFT;
                if(h==1 || h==2 || h==3 || states[i].x > 12000) buttons|=HT_RIGHT;
                /* Provider's canonical mapping is B/A/Y/X: A is bit 1. */
                if(states[i].buttons&(1u<<1)) buttons|=HT_JUMP;
                if(states[i].buttons&1u) buttons|=HT_INTERACT;
                if(states[i].buttons&(1u<<9)) buttons|=HT_PAUSE;
                if(states[i].buttons&(1u<<8)) buttons|=HT_EXIT;
            }
        }
    }
    if(!connected) {
        if(in.buttons&T5_APP_BUTTON_LEFT) buttons|=HT_LEFT;
        if(in.buttons&T5_APP_BUTTON_RIGHT) buttons|=HT_RIGHT;
        if(in.buttons&T5_APP_BUTTON_CONFIRM) buttons|=HT_JUMP;
        if(in.buttons&T5_APP_BUTTON_UP) buttons|=paused?HT_JUMP:HT_INTERACT;
        if(in.buttons&T5_APP_BUTTON_DOWN) buttons|=HT_PAUSE;
        if(in.buttons&T5_APP_BUTTON_BACK) buttons|=HT_EXIT;
    }
    /* Run simulation at input checkpoints, including during rendering. */
    uint32_t now=app->millis();
    ht_advance(now);
    uint32_t down=buttons&~previous;
    if(reading) {
        ht_journal_input(down);
        if(down) ++scene_revision;
        jump_down=pause_down=false; simulation_started=false; simulation_accumulator=0;
        previous=held=buttons; last_poll=now; return;
    }
    jump_down|=(down&HT_JUMP)!=0; pause_down|=(down&HT_PAUSE)!=0;
    if((down&HT_INTERACT) && !paused && !loading && ht.level==ht_geometry_level) {
        int page=ht_inspect();
        if(page>=0) { ht_journal_open((unsigned)page); reading=true; }
        else if(ht_puzzle_near(&ht)>=0) (void)ht_interact();
        else { ht_journal_index=true; ht_journal_selection=0; reading=true; }
        if(reading) { jump_down=pause_down=false; simulation_started=false; simulation_accumulator=0; }
        ++scene_revision;
    }
    quitting|=(down&HT_EXIT)!=0;
    previous=held=buttons; last_poll=now;
}
static void ht_render_service(void) {
    /* Bounded row/column checkpoints plus an 8ms elapsed threshold. Poll(1)
     * really yields; queued input edges survive rendering and busy scans. */
    if(app->millis()-last_poll>=8u) ht_input(1u);
    ht_abort=quitting;
}
#define HT_PACKED_BYTES (HT_W*HT_H/2u)
static void ht_copy_packed(uint8_t *dst,const uint8_t *src) {
    /* Fixed 16-physical-row chunks; service both row and elapsed checkpoints.
     * Never write the driver's queued buffer: caller must hold a free one. */
    for(size_t at=0;at<HT_PACKED_BYTES && !ht_abort;at+=1920u) {
        size_t count=HT_PACKED_BYTES-at;
        if(count>1920u) count=1920u;
        memcpy(dst+at,src+at,count);
        ht_checkpoint();
    }
}
static bool ht_start_video(const t5_video_api_v1 *video,t5_video_surface_v1 *surface) {
    uint8_t format=T5_VIDEO_PIXEL_MONO_1BPP_MSB;
    if(!video->start_format(surface,format)) return false;
    if(surface->width!=960 || surface->height!=540 ||
       surface->stride_bytes!=120 || surface->pixel_format!=format ||
       !(surface->flags&T5_VIDEO_FLAG_ONE_IS_BLACK)) { video->stop(); return false; }
    return true;
}
__attribute__((visibility("default"))) uint32_t app_hardware_takeover(void) {
    return T5_HARDWARE_TAKEOVER_DISPLAY;
}
__attribute__((visibility("default"))) void app_main(void) {
    app=t5_app_get_api(T5_APP_ABI_VERSION);
    const t5_video_api_v1 *video=t5_video_get_api(T5_VIDEO_API_VERSION);
    if(!app || app->abi_version!=T5_APP_ABI_VERSION ||
       !HT_HAS(app,t5_app_api_v1,psram_free) || !app->psram_alloc || !app->poll || !app->millis ||
       !video || video->api_version!=T5_VIDEO_API_VERSION ||
       !HT_HAS(video,t5_video_api_v1,start_format) || !video->backbuffer ||
       !video->can_submit || !video->submit || !video->stop) return;
    uint8_t *memory=(uint8_t *)app->psram_alloc(HT_MEMORY+HT_PACKED_BYTES+31u);
    if(!memory) { ht_log("Hollow Trail: scenery cache PSRAM unavailable"); return; }
    uint8_t *staging=(uint8_t *)app->psram_alloc(HT_PACKED_BYTES);
    if(!staging) ht_log("Hollow Trail: packed staging unavailable; using direct packing");
    bool started=false,display_initialized=false,display_reading=false;
    ht_reader_bitmap=(uint8_t *)(((uintptr_t)memory+HT_MEMORY+31u)&~(uintptr_t)15u);
    t5_video_surface_v1 surface={0};
    if(HT_HAS(app,t5_app_api_v1,set_back_exits_app)) app->set_back_exits_app(false);
    ht_math=t5_math_get_api(T5_MATH_API_VERSION);
    if(ht_math && (ht_math->api_version!=T5_MATH_API_VERSION || ht_math->struct_size<sizeof(*ht_math) ||
                   !ht_math->add_s16 || !ht_math->sub_s16 || !ht_math->copy_bytes || !ht_math->fill_bytes)) ht_math=NULL;
    ht_acquire_pad(); ht_acquire_reader(); ht_bind((uint8_t *)(((uintptr_t)memory+15u)&~(uintptr_t)15u));
    ht_dsp_composite=false;
    if(!ht_start_video(video,&surface)) {
        ht_log("Hollow Trail: video start failed"); goto cleanup;
    }
    started=true;
    memset(&ht,0,sizeof(ht)); ht_spawn(true);
    reading=false; journal_page=0; ht_journal_index=true; ht_journal_selection=0;
    quitting=jump_down=pause_down=paused=mode_down=false; held=previous=0;
    simulation_started=false; simulation_accumulator=0; scene_revision=1;
    ht_service=ht_render_service;
    /* Freeze physics while the first view and lookahead strips are prepared.
     * Keep polling/yielding and honor exit throughout the bounded warmup. */
    loading=true;
    ht_clear_layer(ht_scene); ht_text(150,120,"PREPARING FOREST",2); ht_vignette();
    if(video->can_submit()) {
        size_t size=0; uint8_t *buffer=video->backbuffer(&size);
        if(buffer && size>=(size_t)surface.stride_bytes*surface.height) {
            ht_pack_mono(buffer,surface.stride_bytes);
            display_initialized=video->submit(0,0);
        }
    }
    unsigned warm_steps=0;
    while(!quitting && warm_steps<HT_WARM_STEPS && ht_cache_prefetch(0,1)) ++warm_steps;
    if(warm_steps==HT_WARM_STEPS) { ht_log("Hollow Trail: cache warmup did not finish"); goto cleanup; }
    loading=false; jump_down=pause_down=mode_down=false;
    if(quitting) goto cleanup;
    uint32_t last_frame=app->millis()-HT_FRAME_INTERVAL_MS, last_submit=app->millis();
    uint32_t drawn_revision=0, prepared_revision=0;
    bool prepared=false,prepared_reader=false;
    uint32_t prepared_since=0,prepared_render_ms=0,prepared_pack_ms=0;
    bool prepared_staged=false;
    bool prepared_profile=false;
    bool profile_was_paused=false;
    memset(&ht_perf,0,sizeof(ht_perf));ht_perf.start=app->millis();
    if(video->frame_counter) ht_perf.scan_start=video->frame_counter();
    ht_log("Hollow Trail 1.0.15: dithered 1bpp parallax renderer started");
    while(!quitting) {
        /* A ready frame gets priority over another fixed poll delay. Busy
         * waits still poll/yield every iteration, and ready paths poll by 8ms. */
        if(!prepared || !video->can_submit() || app->millis()-last_poll>=8u)
            ht_input(prepared?1u:4u);
        if(quitting) break;
        if(ht.level!=ht_geometry_level) {
            /* Discard an old-level prepared frame and freeze physics during
             * bounded cache warmup. Keep the last picture on the panel. */
            prepared=false; loading=true;
            ht_select_level(ht.level);
            unsigned steps=0;
            while(!quitting && steps<HT_WARM_STEPS && ht_cache_prefetch(ht.camera/256,1)) ++steps;
            loading=false; simulation_started=false; simulation_accumulator=0;
            jump_down=pause_down=mode_down=false;
            if(quitting) break;
            if(steps==HT_WARM_STEPS) { ht_log("Hollow Trail: level warmup did not finish"); break; }
            ++scene_revision;
            last_submit=app->millis(); last_frame=last_submit-HT_FRAME_INTERVAL_MS;
        }
        if(mode_down) {
            mode_down=false;
            ht_dsp_composite=ht_dsp_available() && !ht_dsp_composite;
            prepared=false; ++scene_revision;
            last_submit=app->millis(); last_frame=last_submit-HT_FRAME_INTERVAL_MS;
            memset(&ht_perf,0,sizeof(ht_perf));ht_perf.start=last_submit;
            if(video->frame_counter) ht_perf.scan_start=video->frame_counter();
        }
        uint32_t now=app->millis();
        if(profile_was_paused && !paused && !reading) {
            ht_perf.start=now;
            ht_perf.scan_start=video->frame_counter?video->frame_counter():0;
            ht_perf.frames=ht_perf.render_ms=ht_perf.pack_ms=ht_perf.wait_ms=ht_perf.cache_ms=ht_perf.copy_ms=0;
        }
        profile_was_paused=paused || reading;
        bool redraw=scene_revision!=drawn_revision;
        if(!redraw && !prepared) last_submit=now;
        /* Render into app-owned PSRAM while the panel finishes its previous
         * scan. Only acquire/write the video backbuffer when it is ready. */
        if(redraw && !prepared && now-last_frame>=HT_FRAME_INTERVAL_MS) {
            last_frame=now; /* Start-to-start cadence, not an extra post-render wait. */
            prepared_revision=scene_revision;
            const ht_game rendering_game=ht;
            const bool rendering_paused=paused,rendering_reading=reading;
            prepared_reader=rendering_reading;
            prepared_profile=!rendering_paused && !rendering_reading;
            if(rendering_reading) ht_journal_render();
            else {
            ht_render_scene();
            /* Initial instructions dismiss automatically after walking. */
            if(rendering_game.x<230*256 && rendering_game.checkpoint==0) {
                ht_rect(ht_scene,90,38,249,68,0);
                ht_text(98,45,ht_chapters[rendering_game.level].title,2);
                ht_text(98,66,"LEFT/RIGHT MOVE   A / CONFIRM JUMP",1);
                ht_text(98,79,"START / DOWN PAUSE   SELECT / BACK EXIT",1);
                ht_text(98,92,"B / UP INSPECT OR OPEN JOURNAL",1);
            }
            if(!rendering_paused) {
                ht_narration(&rendering_game);
                ht_puzzle_prompt(&rendering_game); ht_evidence_prompt(&rendering_game);
            }
            if(rendering_paused) {
                ht_rect(ht_scene,112,74,256,172,0);
                ht_text(127,83,"HOLLOW TRAIL 1.0.15",1);
                ht_text(198,95,"PAUSED",2);
                ht_text(127,117,"START / DOWN RESUME    SELECT / BACK EXIT",1);
                ht_text(127,133,!ht_dsp_available()?"DSP16: UNAVAILABLE":ht_dsp_composite?"DSP16 COMPOSITOR: ON":"DSP16 COMPOSITOR: OFF",1);
                ht_text(127,148,"A / CONFIRM TOGGLE DSP16",1);
                char perf[64];
                snprintf(perf,sizeof(perf),"FPS %lu.%lu   SCANS %lu.%lu",
                    (unsigned long)(ht_perf.fps10/10),(unsigned long)(ht_perf.fps10%10),
                    (unsigned long)(ht_perf.scan10/10),(unsigned long)(ht_perf.scan10%10));
                ht_text(127,164,perf,1);
                snprintf(perf,sizeof(perf),"RENDER %lu PACK %lu WAIT %lu MS",
                    (unsigned long)ht_perf.render_avg,(unsigned long)ht_perf.pack_avg,(unsigned long)ht_perf.wait_avg);
                ht_text(127,179,perf,1);
                snprintf(perf,sizeof(perf),"CACHE %lu COPY %lu MS",
                    (unsigned long)ht_perf.cache_avg,(unsigned long)ht_perf.copy_avg);
                ht_text(127,195,perf,1);
                t5_video_scan_stats_v1 scan={0};
                if(HT_HAS(video,t5_video_api_v1,scan_stats) && video->scan_stats(&scan)) {
                    snprintf(perf,sizeof(perf),"SCAN %lu PREP %lu DMA %lu MS",
                        (unsigned long)((scan.scan_us+500)/1000),
                        (unsigned long)((scan.prepare_us+500)/1000),
                        (unsigned long)((scan.dma_wait_us+500)/1000));
                    ht_text(127,210,perf,1);
                    snprintf(perf,sizeof(perf),"PACE %lu ROWS %lu CORE %u/%u",
                        (unsigned long)((scan.pace_us+500)/1000),
                        (unsigned long)scan.active_rows,(unsigned)scan.scan_core,(unsigned)scan.app_core);
                    ht_text(127,225,perf,1);
                } else ht_text(127,210,"SCAN TIMING NOT AVAILABLE",1);
            }
            } /* game frame */
            prepared_since=app->millis();
            prepared_render_ms=prepared_since-now;
            prepared_pack_ms=0; prepared_staged=false;
            prepared=true;
        }
        if(quitting) break;
        /* Overlap useful packing with the queued scan. Keep the no-copy path
         * when a backbuffer is already available; staging failure is harmless. */
        if(prepared && !prepared_reader && staging && !prepared_staged && !video->can_submit()) {
            uint32_t pack_start=app->millis();
            ht_pack_mono(staging,surface.stride_bytes);
            prepared_since=app->millis();
            prepared_pack_ms=prepared_since-pack_start;
            prepared_staged=true;
        }
        if(quitting) break;
        if(prepared && video->can_submit()) {
            size_t size=0; uint8_t *buffer=video->backbuffer(&size);
            if(!buffer || size<(size_t)surface.stride_bytes*surface.height) {
                ht_log("Hollow Trail: video backbuffer unavailable"); break;
            }
            uint32_t pack_start=app->millis();
            if(prepared_reader) ht_copy_packed(buffer,ht_reader_bitmap);
            else if(prepared_staged) ht_copy_packed(buffer,staging);
            else ht_pack_mono(buffer,surface.stride_bytes);
            uint32_t output_ms=app->millis()-pack_start;
            if(quitting) break;
            bool cropped=display_initialized && !prepared_reader && !display_reading;
            if(video->submit(cropped?ht_dirty_top:0,cropped?ht_dirty_height:0)) {
                display_reading=prepared_reader;
                display_initialized=true;
                last_submit=app->millis(); drawn_revision=prepared_revision; prepared=false;
                if(prepared_profile) {
                    ht_perf.render_ms+=prepared_render_ms;
                    ht_perf.pack_ms+=prepared_pack_ms+(prepared_staged?0:output_ms);
                    ht_perf.copy_ms+=prepared_staged?output_ms:0;
                    ht_perf.wait_ms+=pack_start-prepared_since;
                    ht_perf_finish(video,last_submit);
                }
            }
        }
        /* At most four cache slices and an 8ms elapsed budget per host loop.
         * A single slice may exceed 8ms; its raster checkpoints still yield.
         * Report this work separately rather than hiding it in lower RENDER. */
        if(!quitting && !paused && !reading && ht.level==ht_geometry_level) {
            uint32_t cache_start=app->millis();
            for(unsigned work=0;work<4 && !quitting;++work) {
                if(prepared && video->can_submit()) break;
                if(!ht_cache_prefetch(ht.camera/256,ht.vx<0?-1:1)) break;
                if(app->millis()-cache_start>=8u) break;
            }
            ht_perf.cache_ms+=app->millis()-cache_start;
        }
        if((redraw || prepared) && app->millis()-last_submit>3000u) {
            ht_log("Hollow Trail: video submission stalled"); break;
        }
    }
cleanup:
    ht_service=NULL; loading=false;
    if(started) video->stop();
    ht_release_reader(); ht_release_pad();
    if(HT_HAS(app,t5_app_api_v1,set_back_exits_app)) app->set_back_exits_app(true);
    if(staging) app->psram_free(staging);
    app->psram_free(memory);
}
