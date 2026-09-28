#include "T5AppApi.h"
#include "T5VideoApi.h"
#include "T5HardwareTakeover.h"
#include "T5ProviderCapabilityApi.h"
#include "RiscUsbHidV1.h"
#include "hollow_trail_engine.inc"

static const t5_app_api_v1 *app;
static const t5_provider_capability_api_v1 *caps;
static const risc_usb_gamepad_api_v1 *pad;
static t5_provider_capability_lease_t lease=T5_PROVIDER_CAPABILITY_LEASE_INVALID;
static bool quitting, jump_down, pause_down, paused;
static uint32_t held, previous, last_poll;
static uint32_t simulation_clock, simulation_accumulator, scene_revision;
static bool simulation_started;
#define HT_LEFT 1u
#define HT_RIGHT 2u
#define HT_JUMP 4u
#define HT_PAUSE 8u
#define HT_EXIT 16u
#define HT_HAS(api,type,field) ((api) && (api)->struct_size >= offsetof(type,field)+sizeof((api)->field) && (api)->field)
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
    if(!simulation_started) { simulation_clock=now; simulation_started=true; }
    if(pause_down) {
        paused=!paused; pause_down=false; simulation_accumulator=0; ++scene_revision;
    }
    uint32_t elapsed=now-simulation_clock;
    simulation_clock=now;
    simulation_accumulator+=elapsed>128u?128u:elapsed;
    if(paused) { simulation_accumulator=0; jump_down=false; return; }
    for(unsigned steps=0;simulation_accumulator>=HT_STEP_MS && steps<8;++steps) {
        int direction=((held&HT_RIGHT)!=0)-((held&HT_LEFT)!=0);
        ht_game before=ht;
        ht_step(direction,jump_down,(held&HT_JUMP)!=0);
        jump_down=false; simulation_accumulator-=HT_STEP_MS;
        if(before.x!=ht.x || before.y!=ht.y || before.camera!=ht.camera ||
           before.stride!=ht.stride || before.facing!=ht.facing || before.laps!=ht.laps)
            ++scene_revision;
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
                if(states[i].buttons&(1u<<9)) buttons|=HT_PAUSE;
                if(states[i].buttons&(1u<<8)) buttons|=HT_EXIT;
            }
        }
    }
    if(!connected) {
        if(in.buttons&T5_APP_BUTTON_LEFT) buttons|=HT_LEFT;
        if(in.buttons&T5_APP_BUTTON_RIGHT) buttons|=HT_RIGHT;
        if(in.buttons&(T5_APP_BUTTON_CONFIRM|T5_APP_BUTTON_UP)) buttons|=HT_JUMP;
        if(in.buttons&T5_APP_BUTTON_DOWN) buttons|=HT_PAUSE;
        if(in.buttons&T5_APP_BUTTON_BACK) buttons|=HT_EXIT;
    }
    /* Run simulation at input checkpoints, including during rendering. */
    uint32_t now=app->millis();
    ht_advance(now);
    uint32_t down=buttons&~previous;
    jump_down|=(down&HT_JUMP)!=0; pause_down|=(down&HT_PAUSE)!=0;
    quitting|=(buttons&HT_EXIT)!=0;
    previous=held=buttons; last_poll=now;
}
static void ht_render_service(void) {
    /* Bounded row/column checkpoints plus an 8ms elapsed threshold. Poll(1)
     * really yields; queued input edges survive rendering and busy scans. */
    if(app->millis()-last_poll>=8u) ht_input(1u);
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
    uint8_t *memory=(uint8_t *)app->psram_alloc(HT_MEMORY+15u);
    if(!memory) { ht_log("Hollow Trail: 1296000 bytes PSRAM unavailable"); return; }
    bool started=false;
    t5_video_surface_v1 surface={0};
    if(HT_HAS(app,t5_app_api_v1,set_back_exits_app)) app->set_back_exits_app(false);
    ht_math=t5_math_get_api(T5_MATH_API_VERSION);
    if(ht_math && (ht_math->api_version!=T5_MATH_API_VERSION || ht_math->struct_size<sizeof(*ht_math) ||
                   !ht_math->add_s16 || !ht_math->sub_s16 || !ht_math->copy_bytes || !ht_math->fill_bytes)) ht_math=NULL;
    ht_acquire_pad(); ht_bind((uint8_t *)(((uintptr_t)memory+15u)&~(uintptr_t)15u));
    if(!video->start_format(&surface,T5_VIDEO_PIXEL_GRAY_2BPP_MSB)) {
        ht_log("Hollow Trail: grayscale video start failed"); goto cleanup;
    }
    started=true;
    if(surface.width!=960 || surface.height!=540 || surface.stride_bytes!=240 ||
       surface.pixel_format!=T5_VIDEO_PIXEL_GRAY_2BPP_MSB ||
       !(surface.flags&T5_VIDEO_FLAG_ONE_IS_BLACK)) {
        ht_log("Hollow Trail: unsupported video surface"); goto cleanup;
    }
    memset(&ht,0,sizeof(ht)); ht_spawn(true);
    quitting=jump_down=pause_down=paused=false; held=previous=0;
    simulation_started=false; simulation_accumulator=0; scene_revision=1;
    ht_service=ht_render_service;
    uint32_t last_frame=app->millis()-67u, last_submit=app->millis();
    uint32_t drawn_revision=0, prepared_revision=0;
    bool prepared=false;
    ht_log("Hollow Trail 1.0.2: 2bpp parallax renderer started");
    while(!quitting) {
        ht_input(4u); if(quitting) break;
        uint32_t now=app->millis();
        bool redraw=scene_revision!=drawn_revision;
        if(!redraw && !prepared) last_submit=now;
        /* Render into app-owned PSRAM while the panel finishes its previous
         * scan. Only acquire/write the video backbuffer when it is ready. */
        if(redraw && !prepared && now-last_frame>=67u) {
            last_frame=now; /* Start-to-start cadence, not an extra post-render wait. */
            prepared_revision=scene_revision;
            const ht_game rendering_game=ht;
            const bool rendering_paused=paused;
            ht_render_scene();
            /* Initial instructions dismiss automatically after walking. */
            if(rendering_game.x<230*256 && rendering_game.checkpoint==0) {
                ht_rect(ht_scene,15,15,249,55,0);
                ht_text(23,22,"HOLLOW TRAIL",2);
                ht_text(23,43,"LEFT/RIGHT MOVE   A / CONFIRM JUMP",1);
                ht_text(23,56,"START / DOWN PAUSE   SELECT / BACK EXIT",1);
            }
            if(rendering_paused) {
                ht_rect(ht_scene,112,86,256,55,0);
                ht_text(198,95,"PAUSED",2);
                ht_text(127,121,"START / DOWN RESUME    SELECT / BACK EXIT",1);
            }
            prepared=true;
        }
        if(quitting) break;
        if(prepared && video->can_submit()) {
            size_t size=0; uint8_t *buffer=video->backbuffer(&size);
            if(!buffer || size<(size_t)surface.stride_bytes*surface.height) {
                ht_log("Hollow Trail: video backbuffer unavailable"); break;
            }
            ht_pack(buffer,surface.stride_bytes);
            if(quitting) break;
            if(video->submit(0,0)) {
                last_submit=app->millis(); drawn_revision=prepared_revision; prepared=false;
            }
        }
        if((redraw || prepared) && app->millis()-last_submit>3000u) {
            ht_log("Hollow Trail: video submission stalled"); break;
        }
    }
cleanup:
    ht_service=NULL;
    if(started) video->stop();
    ht_release_pad();
    if(HT_HAS(app,t5_app_api_v1,set_back_exits_app)) app->set_back_exits_app(true);
    app->psram_free(memory);
}
