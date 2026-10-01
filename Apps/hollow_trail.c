#include "T5AppApi.h"
#include <stdio.h>
#include <stdlib.h>
#include "T5VideoApi.h"
#include "T5HardwareTakeover.h"
#include "T5ProviderCapabilityApi.h"
#include "RiscUsbHidV1.h"
#include "RiscReaderTypographyV1.h"
#include "hollow_trail_engine.inc"
#include "hollow_trail_cutscene.inc"

static const t5_app_api_v1 *app;
static const t5_provider_capability_api_v1 *caps;
static const risc_usb_gamepad_api_v1 *pad,*hid_pad;
static t5_provider_capability_lease_t hid_lease;
static bool ht_pad_owned,ht_input_rearm;
static int ht_pad_source=-1;
static uint64_t ht_pad_device;
static bool ht_pad_fault;
static uint32_t ht_pad_fault_since;
static t5_provider_capability_lease_t lease=T5_PROVIDER_CAPABILITY_LEASE_INVALID;
static bool quitting, jump_down, pause_down, paused;
static uint32_t held, previous, last_poll, last_yield;
static uint32_t simulation_clock, simulation_accumulator, scene_revision;
static bool simulation_started,loading,reading;
static unsigned journal_page,debug_level;
static bool debug_select,debug_jump;
#define HT_INPUT_INTERVAL_MS 8u
#define HT_YIELD_INTERVAL_MS 32u
#define HT_FRAME_INTERVAL_MS 42u /* At most 24 submissions/s; physics stays 32ms. */
#include "hollow_trail_fps.inc"
#if HT_FPS_SLOTS < ((HT_FPS_WINDOW_MS+HT_FRAME_INTERVAL_MS-1)/HT_FRAME_INTERVAL_MS+1)
#error "Increase FPS history capacity when raising the frame-rate cap"
#endif
static ht_fps_window ht_fps;
static struct {
    uint32_t start,scan_start,frames,render_ms,pack_ms,wait_ms,cache_ms,copy_ms,input_ms;
    uint32_t fps10,scan10,render_avg,pack_avg,wait_avg,cache_avg,copy_avg,input_avg;
    ht_render_timing stages,stage_avg;
} ht_perf;
static void ht_perf_finish(const t5_video_api_v1 *video,uint32_t now) {
    ++ht_perf.frames;
    ht_perf.fps10=ht_fps_push(&ht_fps,now);
    uint32_t elapsed=now-ht_perf.start;
    if(elapsed<1000u) return;
    uint32_t scans=video->struct_size>=offsetof(t5_video_api_v1,frame_counter)+sizeof(video->frame_counter) && video->frame_counter?video->frame_counter():0;

    ht_perf.scan10=(scans-ht_perf.scan_start)*10000u/elapsed;
    ht_perf.render_avg=ht_perf.render_ms/ht_perf.frames;
    ht_perf.pack_avg=ht_perf.pack_ms/ht_perf.frames;
    ht_perf.wait_avg=ht_perf.wait_ms/ht_perf.frames;
    ht_perf.cache_avg=ht_perf.cache_ms/ht_perf.frames;
    ht_perf.copy_avg=ht_perf.copy_ms/ht_perf.frames;
    ht_perf.input_avg=ht_perf.input_ms/ht_perf.frames;
    ht_perf.stage_avg.background=ht_perf.stages.background/ht_perf.frames;
    ht_perf.stage_avg.world=ht_perf.stages.world/ht_perf.frames;
    ht_perf.stage_avg.post=ht_perf.stages.post/ht_perf.frames;
    memset(&ht_perf.stages,0,sizeof(ht_perf.stages));
    ht_perf.start=now;ht_perf.scan_start=scans;
    ht_perf.frames=ht_perf.render_ms=ht_perf.pack_ms=ht_perf.wait_ms=ht_perf.cache_ms=ht_perf.copy_ms=ht_perf.input_ms=0;
}
#define HT_LEFT 1u
#define HT_RIGHT 2u
#define HT_JUMP 4u
#define HT_INTERACT 32u
#define HT_PAUSE 8u
#define HT_EXIT 16u
#define HT_ACCEPT 64u
#define HT_JOURNAL 128u
#define HT_UP 512u
#define HT_DOWN 1024u
#define HT_BACK 256u /* Reading navigation only; never quits gameplay. */
#define HT_HAS(api,type,field) ((api) && (api)->struct_size >= offsetof(type,field)+sizeof((api)->field) && (api)->field)
#include "hollow_trail_journal.inc"
static void ht_log(const char *message) {
    if(HT_HAS(app,t5_app_api_v1,log_message)) app->log_message(message);
}
static void ht_release_pad(void) {
    if(lease!=T5_PROVIDER_CAPABILITY_LEASE_INVALID && caps) (void)caps->release(lease);
    if(hid_lease && caps) (void)caps->release(hid_lease);
    hid_lease=0; hid_pad=NULL;
    lease=T5_PROVIDER_CAPABILITY_LEASE_INVALID; pad=NULL; caps=NULL;
}
static void ht_acquire_pad(void) {
    caps=t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if(!caps || caps->api_version!=T5_PROVIDER_CAPABILITY_API_VERSION ||
       !HT_HAS(caps,t5_provider_capability_api_v1,acquire) ||
       !HT_HAS(caps,t5_provider_capability_api_v1,release)) { caps=NULL; return; }
    const char *names[]={"usb.xinput.gamepad","usb.hid.gamepad"};
    for(unsigned i=0;i<2;++i) {
        const void *api=NULL; t5_provider_capability_lease_t token=0;
        if(!caps->acquire(names[i],RISC_USB_GAMEPAD_API_V1,&token,&api)) continue;
        const risc_usb_gamepad_api_v1 *candidate=(const risc_usb_gamepad_api_v1 *)api;
        if(!candidate || candidate->api_version!=1 ||
           !HT_HAS(candidate,risc_usb_gamepad_api_v1,snapshot) || !candidate->poll) {
            if(token) (void)caps->release(token);
            continue;
        }
        if(i) { hid_pad=candidate; hid_lease=token; }
        else { pad=candidate; lease=token; }
    }
}
static void ht_advance(uint32_t now) {
    if(reading || loading || debug_jump || ht.level!=ht_geometry_level) { ht_motion_emphasis=false; simulation_started=false; simulation_accumulator=0; jump_down=pause_down=false; return; }
    if(!simulation_started) { simulation_clock=now; simulation_started=true; }
    if(ht_cutscene.active) {
        uint32_t elapsed=now-simulation_clock;
        simulation_clock=now;
        simulation_accumulator+=elapsed>128u?128u:elapsed;
        for(unsigned steps=0;simulation_accumulator>=HT_STEP_MS && steps<8;++steps) {
            bool finished=ht_cutscene_step(&ht_cutscene);
            simulation_accumulator-=HT_STEP_MS;++scene_revision;
            if(finished) {
                ht_cutscene_apply_handoff(&ht_cutscene);
                held=previous=0;jump_down=pause_down=false;ht_input_rearm=true;
                simulation_accumulator=0;break;
            }
        }
        ht_motion_emphasis=false;
        return;
    }
    if(pause_down) {
        paused=!paused; debug_select=false; debug_level=ht.level; pause_down=false; simulation_accumulator=0; ++scene_revision;
    }
    uint32_t elapsed=now-simulation_clock;
    simulation_clock=now;
    simulation_accumulator+=elapsed>128u?128u:elapsed;
    if(paused) { simulation_accumulator=0; jump_down=false; ht_motion_emphasis=false; return; }
    for(unsigned steps=0;simulation_accumulator>=HT_STEP_MS && steps<8;++steps) {
        int direction=((held&HT_RIGHT)!=0)-((held&HT_LEFT)!=0);
        ht_motion_emphasis=(held&HT_BACK)!=0;
        ht_game before=ht;
        ht_step_controls(direction,((held&HT_DOWN)!=0)-((held&HT_UP)!=0),jump_down,(held&HT_JUMP)!=0);
        jump_down=false; simulation_accumulator-=HT_STEP_MS;
        if(before.ticks!=ht.ticks || before.x!=ht.x || before.y!=ht.y || before.camera!=ht.camera ||
           before.camera_y!=ht.camera_y || memcmp(&before.traversal,&ht.traversal,sizeof(ht.traversal)) ||
           before.story_x!=ht.story_x || before.level!=ht.level || before.stride!=ht.stride || before.facing!=ht.facing || before.laps!=ht.laps)
            ++scene_revision;
        if(ht_cutscene_mill_arrival(&before,&ht)) {
            ht_cutscene_begin(HT_CUTSCENE_MILL);
            held=previous=0;jump_down=pause_down=false;
            simulation_accumulator=0;++scene_revision;break;
        }
        if(before.level!=ht.level) {
            if(ht_cutscene_city_arrival(&before,&ht)) {
                ht_cutscene_begin(HT_CUTSCENE_CITY);
                held=previous=0;jump_down=pause_down=false;++scene_revision;
            }
            simulation_accumulator=0;break;
        }
    }
}
static void ht_input_update(uint32_t wait) {
    t5_app_input_t in={0};
    bool ok;
    if(!wait && HT_HAS(app,t5_app_api_v1,poll_nowait)) ok=app->poll_nowait(&in);
    else {
        ok=app->poll(&in,wait?wait:1u);
        last_yield=app->millis();
    }
    if(!ok || in.exit_requested) { quitting=true; return; }
    uint32_t buttons=0; bool connected=false,fault=false,raw_buttons=false;
    int selected_source=-1; uint64_t selected_device=0;
    const risc_usb_gamepad_api_v1 *providers[]={pad,hid_pad};
    for(unsigned source=0;source<2 && !connected;++source) {
        const risc_usb_gamepad_api_v1 *provider=providers[source];
        if(!provider) continue;
        risc_usb_gamepad_state_v1 states[4]; size_t count=4;
        memset(states,0,sizeof(states));
        if(!provider->poll(provider->context,8u) ||
           !provider->snapshot(provider->context,states,&count) || count>4) { fault=true; continue; }
        for(size_t i=0;i<count;++i) if(states[i].connected) {
            connected=true; selected_source=(int)source; selected_device=states[i].device;
            const risc_usb_gamepad_state_v1 *state=&states[i];
            raw_buttons=state->buttons!=0;
            uint8_t h=state->hat;
            if(h>=5 && h<=7) buttons|=HT_LEFT;
            if(h>=1 && h<=3) buttons|=HT_RIGHT;
            if(h==7 || h==0 || h==1) buttons|=HT_UP;
            if(h>=3 && h<=5) buttons|=HT_DOWN;
            if(!source || h>=8) {
                if(state->x < -16384) buttons|=HT_LEFT;
                if(state->x > 16384) buttons|=HT_RIGHT;
                if(state->y < -16384) buttons|=HT_UP;
                if(state->y > 16384) buttons|=HT_DOWN;
            }
            /* Receiver face labels confirmed by the owner's 1.0.15 test:
             * A/B are the reverse of the earlier GameBoy adapter assumption.
             * Keep the independently working X/Start/Select masks unchanged. */
            if(state->buttons&(source?0x01u:0x02u)) buttons|=HT_JUMP;
            if(state->buttons&(source?0x02u:0x01u)) buttons|=HT_INTERACT|HT_ACCEPT;
            if(state->buttons&(source?0x04u:0x08u)) buttons|=HT_BACK;
            if(state->buttons&(source?0x80u:0x200u)) buttons|=HT_JOURNAL;
            if(state->buttons&(source?0x40u:0x100u)) buttons|=HT_PAUSE;
            break; // One controller owns the frame; never merge receiver slots.
        }
    }
    if(!connected && fault && ht_pad_owned) {
        /* A failed raw poll is not a disconnect. Do not reinterpret duplicate
         * OS navigation as a new action or leave movement held through it. */
        ht_input_rearm=true;
        if(!ht_pad_fault) { ht_pad_fault=true; ht_pad_fault_since=app->millis(); }
        if(app->millis()-ht_pad_fault_since>=250u) {
            ht_pad_owned=false; ht_pad_source=-1; ht_pad_device=0;
        } // Persistent failure yields to neutral-gated device input, never traps Back forever.
    } else {
        ht_pad_fault=false;
        if(connected!=ht_pad_owned || (connected &&
           (selected_source!=ht_pad_source || selected_device!=ht_pad_device))) ht_input_rearm=true;
        ht_pad_source=selected_source; ht_pad_device=selected_device;
        ht_pad_owned=connected;
        if(!connected) {
            if(in.buttons&T5_APP_BUTTON_LEFT) buttons|=HT_LEFT;
            if(in.buttons&T5_APP_BUTTON_RIGHT) buttons|=HT_RIGHT;
            if(in.buttons&T5_APP_BUTTON_UP) buttons|=(reading || (!paused && (ht_tree_near(&ht)>=0 || ht.traversal.mode==HT_TREE || ht.traversal.support==5 || ht_ladder_near(&ht)>=0 || ht.traversal.mode==HT_LADDER || ht.traversal.mode==HT_LEDGE)))?HT_UP:HT_JUMP;
            if(in.buttons&T5_APP_BUTTON_CONFIRM) buttons|=HT_INTERACT|HT_ACCEPT;
            if(in.buttons&T5_APP_BUTTON_DOWN) buttons|=(reading || (!paused && (ht_tree_near(&ht)>=0 || ht.traversal.mode==HT_TREE || ht.traversal.support==5 || ht_ladder_near(&ht)>=0 || ht.traversal.mode==HT_LADDER || ht.traversal.mode==HT_LEDGE)))?HT_DOWN:HT_PAUSE;
            if(in.buttons&T5_APP_BUTTON_BACK) buttons|=HT_EXIT;
        }
        /* Raw capability ownership suppresses duplicate OS pad navigation.
         * Keep device Back available with a neutral pad, but never interpret
         * a face-button report plus mapped Back as an app-exit action. */
        if(connected && !raw_buttons && (in.buttons&T5_APP_BUTTON_BACK)) buttons|=HT_EXIT;
        if(ht_input_rearm && !buttons) ht_input_rearm=false;
    }
    if(ht_input_rearm) buttons=0; // Release before accepting a new source/recovered report.
    /* Run simulation at input checkpoints, including during rendering. */
    uint32_t now=app->millis();
    ht_advance(now);
    if(ht_input_rearm) buttons=0; /* A cutscene handoff also requires neutral. */
    if(ht_cutscene.active) {
        quitting|=(buttons&HT_EXIT)!=0;
        previous=held=0;jump_down=pause_down=false;last_poll=now;return;
    }
    uint32_t down=buttons&~previous;
    if(reading) {
        if(down&HT_JOURNAL) { reading=false; ht_journal_deciding=ht_journal_confirm=false; }
        else ht_journal_input(down);
        if(down) ++scene_revision;
        jump_down=pause_down=false; simulation_started=false; simulation_accumulator=0;
        previous=held=buttons; last_poll=now; return;
    }
    if(paused && !loading) {
        if(down&(HT_JUMP|HT_UP)) {
            ht_camera_mode=(ht_camera_mode+1)%HT_CAMERA_MODES;
            ++scene_revision;
        }
        down&=~(HT_JUMP|HT_UP); // Paused presses must not queue a jump on resume.
        if(down&(HT_LEFT|HT_RIGHT)) {
            if(!debug_select) debug_level=ht.level;
            debug_level=(debug_level+((down&HT_RIGHT)?1:HT_LEVELS-1))%HT_LEVELS;
            debug_select=true;++scene_revision;
        }
        if(debug_select && (down&(HT_BACK|HT_EXIT))) {
            debug_select=false;down&=~(HT_BACK|HT_EXIT);++scene_revision;
        } else if(debug_select && (down&HT_ACCEPT)) {
            ht.level=debug_level;ht_spawn(true);debug_jump=true;
            paused=reading=debug_select=false;
            jump_down=pause_down=false;
            simulation_started=false;simulation_accumulator=0;++scene_revision;
            /* Loading consumes this press; require neutral before gameplay. */
            ht_input_rearm=true;previous=buttons;held=0;last_poll=now;return;
        }
    }
    jump_down|=(down&HT_JUMP)!=0; pause_down|=(down&HT_PAUSE)!=0;
    if(((down&HT_JOURNAL) || (paused && (down&HT_ACCEPT))) && !loading) {
        ht_journal_index=true; ht_journal_selection=0; ht_journal_deciding=false; reading=true;
        jump_down=pause_down=false; simulation_started=false; simulation_accumulator=0; ++scene_revision;
    }
    if(!reading && (down&HT_INTERACT) && !paused && !loading && ht.level==ht_geometry_level) {
        int page=ht_final_near(&ht)?-1:ht_inspect();
        if(ht_final_near(&ht)) { ht_journal_tower(); reading=true; }
        else if(page>=0) { ht_journal_open((unsigned)page); reading=true; }
        else if(ht_traversal_interact()) { /* A grabs/releases a nearby traversal object. */ }
        else if(ht_puzzle_near(&ht)>=0) (void)ht_interact();
        else (void)ht_observe();

        if(reading) { jump_down=pause_down=false; simulation_started=false; simulation_accumulator=0; }
        ++scene_revision;
    }
    quitting|=(down&HT_EXIT)!=0;
    previous=held=buttons; last_poll=now;
}
/* Keep input cadence separate from real scheduler cooperation. An explicit
 * wait still yields during idle/display waits; raster checkpoints only wait
 * when the 32ms cooperation deadline is due. Older hosts retain safe polling. */
static void ht_input(uint32_t wait) {
    uint32_t start=app->millis();
    if(!wait && start-last_yield>=HT_YIELD_INTERVAL_MS) wait=1u;
    ht_input_update(wait);
    ht_perf.input_ms+=app->millis()-start;
}
static void ht_render_service(void) {
    uint32_t now=app->millis();
    if(now-last_poll>=HT_INPUT_INTERVAL_MS || now-last_yield>=HT_YIELD_INTERVAL_MS)
        ht_input(0u);
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
    uint8_t *memory=(uint8_t *)app->psram_alloc(HT_MEMORY+HT_NATIVE_MEMORY+HT_PACKED_BYTES+31u);
    if(!memory) { ht_log("Hollow Trail: scenery/native-raster PSRAM unavailable"); return; }
    uint8_t *arena=(uint8_t *)(((uintptr_t)memory+15u)&~(uintptr_t)15u);
    uint8_t *staging=(uint8_t *)app->psram_alloc(HT_PACKED_BYTES);
    if(!staging) ht_log("Hollow Trail: packed staging unavailable; using direct packing");
    bool started=false,display_initialized=false,display_reading=false;
    ht_reader_bitmap=arena+HT_MEMORY+HT_NATIVE_MEMORY;
    t5_video_surface_v1 surface={0};
    if(HT_HAS(app,t5_app_api_v1,set_back_exits_app)) app->set_back_exits_app(false);
    ht_math=t5_math_get_api(T5_MATH_API_VERSION);
    if(ht_math && (ht_math->api_version!=T5_MATH_API_VERSION || ht_math->struct_size<sizeof(*ht_math) ||
                   !ht_math->add_s16 || !ht_math->sub_s16 || !ht_math->copy_bytes || !ht_math->fill_bytes)) ht_math=NULL;
    ht_pad_owned=ht_input_rearm=ht_pad_fault=false; debug_select=debug_jump=false; ht_pad_source=-1; ht_pad_device=0;
    ht_acquire_pad(); ht_acquire_reader(); ht_bind(arena); ht_bind_native(arena);
    ht_render_clock=app->millis;
    if(!ht_start_video(video,&surface)) {
        ht_log("Hollow Trail: video start failed"); goto cleanup;
    }
    started=true;
    memset(&ht,0,sizeof(ht)); ht_spawn(true); ht_cutscene_seen=0; ht_cutscene_begin(HT_CUTSCENE_INTRO);
    reading=false; journal_page=0; ht_journal_index=true; ht_journal_selection=0;
    ht_journal_deciding=ht_journal_confirm=ht_journal_page_ready=false; ht_read_submitted_revision=0;
    ht_camera_mode=HT_CAMERA_BASELINE;
    quitting=jump_down=pause_down=paused=false; held=previous=0;
    simulation_started=false; simulation_accumulator=0; scene_revision=1;
    last_poll=last_yield=app->millis();
    ht_service=ht_render_service;
    /* Freeze physics while the first view and lookahead strips are prepared.
     * Keep polling/yielding and honor exit throughout the bounded warmup. */
    loading=true;
    ht_simd_ready=ht_simd_selftest();
    ht_simd_stage_ready=0;
    for(unsigned stage=0;stage<4;++stage) {
        bool ready=ht_simd_ready && ht_expanded_selftest(1u<<stage);
        ht_stage_reason[stage]=ht_simd_ready?ht_expanded_reason:ht_simd_reason;
        if(ready)ht_simd_stage_ready|=1u<<stage;
        char status[144];
        snprintf(status,sizeof(status),"SIMD stage %u: %s pattern=%u phase=%u byte=%u expected=%u actual=%u",
            stage+1,ht_stage_reason[stage],ht_simd_test_pattern,ht_simd_test_phase,ht_simd_test_byte,
            ht_simd_test_expected,ht_simd_test_actual);ht_log(status);
    }
    ht_expanded_ready=ht_simd_stage_ready==HT_OPT_SIMD_ALL;
    ht_log(ht_expanded_ready?"Hollow Trail expanded SIMD: device self-test passed":
        "Hollow Trail: unavailable SIMD stages use scalar fallback");
    ht_log(ht_simd_ready?"Hollow Trail fused SIMD: device self-test passed":
        "Hollow Trail fused SIMD unavailable: AI fallback");
    {
        char diagnostic[160];
        snprintf(diagnostic,sizeof(diagnostic),"SIMD pack=%s expanded=%s features=%lx pattern=%u phase=%u byte=%u expected=%u actual=%u",
            ht_simd_reason,ht_expanded_reason,(unsigned long)(ht_math?ht_math->features:0),
            ht_simd_test_pattern,ht_simd_test_phase,ht_simd_test_byte,ht_simd_test_expected,ht_simd_test_actual);
        ht_log(diagnostic);
    }
    ht_clear_layer(ht_scene); ht_text(168,120,"HOLLOW TRAIL",2); ht_vignette();
    if(video->can_submit()) {
        size_t size=0; uint8_t *buffer=video->backbuffer(&size);
        if(buffer && size>=(size_t)surface.stride_bytes*surface.height) {
            ht_pack_mono(buffer,surface.stride_bytes);
            display_initialized=video->submit(0,0);
        }
    }
    unsigned warm_steps=0;
    while(!quitting && ht.level==4 && warm_steps<HT_WARM_STEPS && ht_cache_prefetch(0,1)) ++warm_steps;
    if(warm_steps==HT_WARM_STEPS) { ht_log("Hollow Trail: cache warmup did not finish"); goto cleanup; }
    loading=false; jump_down=pause_down=false;
    if(quitting) goto cleanup;
    uint32_t last_frame=app->millis()-HT_FRAME_INTERVAL_MS, last_submit=app->millis();
    uint32_t drawn_revision=0, prepared_revision=0;
    bool prepared=false,prepared_reader=false;
    uint32_t prepared_since=0,prepared_render_ms=0,prepared_pack_ms=0;
    bool prepared_staged=false;
    bool prepared_profile=false;
    ht_render_timing prepared_timing={0};
    bool profile_was_paused=false;
    memset(&ht_perf,0,sizeof(ht_perf));ht_perf.start=app->millis();
    if(video->frame_counter) ht_perf.scan_start=video->frame_counter();
    ht_fps_reset(&ht_fps,ht_perf.start);
    ht_log("Hollow Trail 1.1.35: native 960x540 A/B + Forward-X cutscenes; rolling 10-second FPS");
    ht_log(HT_HAS(app,t5_app_api_v1,poll_nowait)?
        "Hollow Trail input: no-wait updates; scheduler yield every 32ms":
        "Hollow Trail input: legacy yielding poll (firmware lacks poll_nowait)");
    while(!quitting) {
        /* Sleep only while idle, pacing, or waiting for the display. Ready
         * work samples on the same cadence as raster checkpoints, without
         * another unconditional delay or a tight input-polling spin. */
        uint32_t input_now=app->millis();
        bool input_wait=prepared?!video->can_submit():
            (scene_revision==drawn_revision || input_now-last_frame<HT_FRAME_INTERVAL_MS);
        if(input_wait || input_now-last_poll>=HT_INPUT_INTERVAL_MS ||
           input_now-last_yield>=HT_YIELD_INTERVAL_MS)
            ht_input(input_wait?1u:0u);
        if(quitting) break;
        if(debug_jump || ht.level!=ht_geometry_level) {
            debug_jump=false;
            /* Discard an old-level prepared frame and freeze physics during
             * bounded cache warmup. Keep the last picture on the panel. */
            prepared=false; loading=true;
            ht_select_level(ht.level);
            unsigned steps=0;
            while(!quitting && ht.level==4 && steps<HT_WARM_STEPS && ht_cache_prefetch(ht.camera/256,1)) ++steps;
            loading=false; simulation_started=false; simulation_accumulator=0;
            jump_down=pause_down=false;
            if(quitting) break;
            if(steps==HT_WARM_STEPS) { ht_log("Hollow Trail: level warmup did not finish"); break; }
            ++scene_revision;
            last_submit=app->millis(); last_frame=last_submit-HT_FRAME_INTERVAL_MS;
        }

        uint32_t now=app->millis();
        if(profile_was_paused && !paused && !reading) {
            ht_fps_reset(&ht_fps,now);ht_perf.fps10=0;
            ht_perf.start=now;
            ht_perf.scan_start=video->frame_counter?video->frame_counter():0;
            ht_perf.frames=ht_perf.render_ms=ht_perf.pack_ms=ht_perf.wait_ms=ht_perf.cache_ms=ht_perf.copy_ms=ht_perf.input_ms=0;
            memset(&ht_perf.stages,0,sizeof(ht_perf.stages));
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
            ht_cutscene_state cutscene_frame=ht_cutscene;
            if(cutscene_frame.active) ht_cutscene_render(&cutscene_frame);
            else {
            ht_render_scene();
            prepared_timing=ht_render_last;
            /* Initial instructions dismiss automatically after walking. */
            if(rendering_game.x<230*256 && rendering_game.checkpoint==0) {
                ht_rect(ht_scene,72,38,336,81,0);
                ht_text(98,45,ht_chapters[rendering_game.level].title,2);
                ht_text(98,66,"LEFT/RIGHT MOVE   B / UP JUMP",1);
                ht_text(98,79,"SELECT / DOWN PAUSE   HOME/BACK EXIT",1);
                ht_text(98,92,"A GRAB / INSPECT   UP/DOWN CLIMB",1);
                ht_text(98,105,"START JOURNAL   X BACK WHILE READING",1);
            }
            if(!rendering_paused) {
                ht_narration(&rendering_game);
                ht_observation_prompt(&rendering_game);
                ht_traversal_prompt(&rendering_game); ht_puzzle_prompt(&rendering_game); ht_evidence_prompt(&rendering_game);
            }
            if(rendering_paused) {
                ht_rect(ht_scene,72,40,336,216,0);
                ht_text(88,48,"HOLLOW TRAIL 1.1.36",1);
                ht_text(192,60,"PAUSED",2);
                char chapter[64];
                snprintf(chapter,sizeof(chapter),"LEVEL %02u / %s",(debug_select?debug_level:ht.level)+1,
                    ht_chapters[debug_select?debug_level:ht.level].title);
                ht_text(88,84,chapter,1);
                ht_text(88,99,debug_select?"L/R CHOOSE   A LOAD   X CANCEL":"L/R CHOOSE LEVEL   A JOURNAL",1);
                ht_text(88,117,"SELECT / DOWN RESUME   HOME/BACK EXIT",1);
                char test[64];
                snprintf(test,sizeof(test),"B/UP GRAPHICS: %s",ht_camera_labels[ht_camera_mode]);
                ht_text(88,133,test,1);
                if(!ht_simd_ready) {
                    snprintf(test,sizeof(test),"PACK FALLBACK: %s",ht_simd_reason);ht_text(88,148,test,1);
                } else if(!ht_expanded_ready) {
                    unsigned missing=HT_OPT_SIMD_ALL&~ht_simd_stage_ready,stage=0;
                    while(stage<3 && !(missing&(1u<<stage)))++stage;
                    snprintf(test,sizeof(test),"STAGE FALLBACK: %s",ht_stage_reason[stage]);ht_text(88,148,test,1);
                } else ht_text(88,148,"START JOURNAL",1);
                char perf[64];
                snprintf(perf,sizeof(perf),"FPS10S %lu.%lu (%lu.%luS) SCANS %lu.%lu",
                    (unsigned long)(ht_perf.fps10/10),(unsigned long)(ht_perf.fps10%10),
                    (unsigned long)(ht_fps.elapsed/1000),(unsigned long)(ht_fps.elapsed%1000/100),
                    (unsigned long)(ht_perf.scan10/10),(unsigned long)(ht_perf.scan10%10));
                ht_text(88,164,perf,1);
                snprintf(perf,sizeof(perf),"RENDER %lu PACK %lu WAIT %lu MS",
                    (unsigned long)ht_perf.render_avg,(unsigned long)ht_perf.pack_avg,(unsigned long)ht_perf.wait_avg);
                ht_text(88,179,perf,1);
                snprintf(perf,sizeof(perf),"CACHE %lu COPY %lu INPUT %lu MS",
                    (unsigned long)ht_perf.cache_avg,(unsigned long)ht_perf.copy_avg,(unsigned long)ht_perf.input_avg);
                ht_text(88,195,perf,1);
                snprintf(perf,sizeof(perf),"BG %lu WORLD %lu CAMERA/EDGE %lu MS",
                    (unsigned long)ht_perf.stage_avg.background,(unsigned long)ht_perf.stage_avg.world,
                    (unsigned long)ht_perf.stage_avg.post);
                ht_text(88,210,perf,1);
                t5_video_scan_stats_v1 scan={0};
                if(HT_HAS(video,t5_video_api_v1,scan_stats) && video->scan_stats(&scan)) {
                    snprintf(perf,sizeof(perf),"SCAN %lu PREP %lu DMA %lu MS",
                        (unsigned long)((scan.scan_us+500)/1000),
                        (unsigned long)((scan.prepare_us+500)/1000),
                        (unsigned long)((scan.dma_wait_us+500)/1000));
                    ht_text(88,225,perf,1);
                    snprintf(perf,sizeof(perf),"PACE %lu ROWS %lu CORE %u/%u",
                        (unsigned long)((scan.pace_us+500)/1000),
                        (unsigned long)scan.active_rows,(unsigned)scan.scan_core,(unsigned)scan.app_core);
                    ht_text(88,240,perf,1);
                } else ht_text(88,225,"SCAN TIMING NOT AVAILABLE",1);
            }
            } /* live game frame */
            } /* game/cutscene frame */
            prepared_since=app->millis();
            prepared_render_ms=prepared_since-now;
            prepared_pack_ms=0; prepared_staged=false;
            prepared=true;
        }
        if(quitting) break;
        if(debug_jump) {prepared=false;continue;}
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
            if(debug_jump) {prepared=false;continue;}
            bool cropped=display_initialized && !prepared_reader && !display_reading;
            if(video->submit(cropped?ht_dirty_top:0,cropped?ht_dirty_height:0)) {
                display_reading=prepared_reader;
                if(prepared_reader) ht_read_submitted_revision=prepared_revision;
                display_initialized=true;
                last_submit=app->millis(); drawn_revision=prepared_revision; prepared=false;
                if(prepared_profile) {
                    ht_perf.stages.background+=prepared_timing.background;
                    ht_perf.stages.world+=prepared_timing.world;
                    ht_perf.stages.post+=prepared_timing.post;
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
        if(!quitting && !paused && !reading && ht.level==4 && ht.level==ht_geometry_level) {
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
    ht_service=NULL;ht_render_clock=NULL; loading=false;
    if(started) video->stop();
    ht_release_reader(); ht_release_pad();
    if(HT_HAS(app,t5_app_api_v1,set_back_exits_app)) app->set_back_exits_app(true);
    if(staging) app->psram_free(staging);
    app->psram_free(memory);
}
