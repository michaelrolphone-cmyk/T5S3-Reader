#!/usr/bin/env python3
"""Execute production controller/motion code and viewer integration on the host."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = (ROOT / "Apps/model_viewer.c").read_text()


def function(name):
    match = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\(", APP, re.M)
    if not match:
        raise AssertionError(f"Missing production function: {name}")
    end = APP.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (APP[end] == "{") - (APP[end] == "}")
        end += 1
    return APP[match.start():end] + "\n"


HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "model_viewer_controls.h"
#define T5_APP_BUTTON_BACK 1u
#define T5_APP_BUTTON_CONFIRM 2u
#define T5_APP_BUTTON_LEFT 4u
#define T5_APP_BUTTON_RIGHT 8u
#define T5_APP_BUTTON_UP 16u
#define T5_APP_BUTTON_DOWN 32u
#define MV_PI 3.14159265358979323846f
#define MV_TWO_PI 6.28318530717958647692f
typedef struct { uint32_t buttons; bool tapped; int16_t touch_x,touch_y; bool exit_requested; } t5_app_input_t;
typedef struct { float yaw,pitch,zoom,pan_x,pan_y; } mv_view_t;
static mv_view_t g_view;
static mv_controller_t g_controller;
static mv_motion_t g_motion;
static uint32_t g_last_interaction_ms,g_render_service_ms;
static bool g_need_refine,g_draw_pending,g_render_interactive,g_render_input_pending;
static t5_app_input_t g_render_input;
static bool ready=true;
static uint32_t clock_ms;
static t5_app_input_t input_state;
static bool can_submit(void) { return ready; }
static uint32_t millis_now(void) { return clock_ms; }
static bool input_poll(t5_app_input_t *out,uint32_t wait) { assert(wait==1u); *out=input_state; return true; }
static const struct { bool (*can_submit)(void); } video={can_submit}, *g_video=&video;
static const struct { uint32_t (*millis)(void); bool (*poll)(t5_app_input_t *,uint32_t); } app={millis_now,input_poll}, *g_app=&app;
'''

TESTS = r'''
typedef struct {
    risc_usb_gamepad_state_v1 states[4];
    size_t count;
    bool poll_ok,snapshot_ok;
    unsigned polls;
} mock_pad_t;
static mock_pad_t mock[2];
static risc_usb_gamepad_api_v1 apis[2];
static unsigned acquired,released,next_calls;
static unsigned available=3;
static bool provider_poll(void *ctx,size_t budget) {
    mock_pad_t *m=ctx; assert(budget==4); ++m->polls; return m->poll_ok;
}
static bool snapshot(void *ctx,risc_usb_gamepad_state_v1 *out,size_t *count) {
    mock_pad_t *m=ctx; assert(*count==4); memcpy(out,m->states,sizeof(m->states)); *count=m->count; return m->snapshot_ok;
}
static int32_t no_events(void *ctx,uint64_t sub,risc_usb_gamepad_event_v1 *ev) {
    (void)ctx;(void)sub;(void)ev; ++next_calls; assert(!"Gamepads must not consume history"); return 0;
}
static bool acquire(const char *name,uint32_t version,t5_provider_capability_lease_t *lease,const void **api) {
    unsigned i=!strcmp(name,"usb.xinput.gamepad");
    assert(i || !strcmp(name,"usb.hid.gamepad")); assert(version==1); ++acquired;
    if(!(available&(1u<<i))) return false;
    *lease=i+1; *api=&apis[i]; return true;
}
static bool release(t5_provider_capability_lease_t lease) { assert(lease==1 || lease==2); ++released; return true; }
static const t5_provider_capability_api_v1 caps={1,sizeof(caps),acquire,release,NULL};
static void mocks_reset(void) {
    memset(mock,0,sizeof(mock)); acquired=released=next_calls=0; available=3;
    for(unsigned i=0;i<2;++i) {
        mock[i].poll_ok=mock[i].snapshot_ok=true;
        apis[i]=(risc_usb_gamepad_api_v1){.api_version=1,.struct_size=sizeof(apis[i]),.context=&mock[i],
            .poll=provider_poll,.next=no_events,.snapshot=snapshot};
    }
}
static void connect_pad(unsigned i,uint64_t id,uint8_t hat,uint32_t buttons) {
    mock[i].count=1;
    mock[i].states[0]=(risc_usb_gamepad_state_v1){.device=id,.connected=1,.hat=hat,.buttons=buttons};
}
static void mapping_tests(void) {
    const uint32_t hats[]={MV_PAD_UP,MV_PAD_UP|MV_PAD_RIGHT,MV_PAD_RIGHT,MV_PAD_RIGHT|MV_PAD_DOWN,
        MV_PAD_DOWN,MV_PAD_DOWN|MV_PAD_LEFT,MV_PAD_LEFT,MV_PAD_LEFT|MV_PAD_UP,0};
    for(unsigned i=0;i<9;++i) {
        risc_usb_gamepad_state_v1 p={.connected=1,.hat=(uint8_t)i};
        assert(mv_pad_decode(&p,false)==hats[i]); assert(mv_pad_decode(&p,true)==hats[i]);
    }
    risc_usb_gamepad_state_v1 p={.connected=1,.hat=8,.x=-32768,.y=32767,.buttons=0x30};
    assert(mv_pad_decode(&p,false)==(MV_PAD_LEFT|MV_PAD_DOWN|MV_PAD_LB|MV_PAD_RB));
    p.x=p.y=16000;p.buttons=1;
    assert(mv_pad_decode(&p,false)==MV_PAD_FINE);assert(mv_pad_decode(&p,true)==MV_PAD_BACK);
    p.buttons=2;assert(mv_pad_decode(&p,false)==MV_PAD_BACK);assert(mv_pad_decode(&p,true)==MV_PAD_FINE);
    p.connected=0;assert(!mv_pad_decode(&p,false));
    puts("mapping: hats, diagonals, SNES axis D-pad, shoulders, HID/XInput A/Back PASS");
}
static void provider_tests(void) {
    mocks_reset();mv_controller_t c;
    mv_controller_open(&c,&caps); assert(acquired==2 && released==0);
    connect_pad(0,10,2,0); assert(!mv_controller_poll(&c)); /* held across launch */
    mock[0].states[0].hat=8; assert(!mv_controller_poll(&c));
    mock[0].states[0].hat=2;
    for(unsigned i=0;i<100;++i) assert(mv_controller_poll(&c)==MV_PAD_RIGHT);
    mock[0].states[0].buttons=0x20;assert(mv_controller_poll(&c)==(MV_PAD_RIGHT|MV_PAD_RB));
    mock[0].poll_ok=false;assert(!mv_controller_poll(&c));
    mock[0].poll_ok=true;assert(!mv_controller_poll(&c)); /* rearm after fault */
    connect_pad(0,10,8,0);assert(!mv_controller_poll(&c));
    connect_pad(0,10,0,0x10);assert(mv_controller_poll(&c)==(MV_PAD_UP|MV_PAD_LB));
    mock[0].snapshot_ok=false;assert(!mv_controller_poll(&c));mock[0].snapshot_ok=true;
    mock[0].count=5;assert(!mv_controller_poll(&c));
    mock[0].count=0;assert(!mv_controller_poll(&c));
    connect_pad(0,11,0,0x10);assert(!mv_controller_poll(&c));
    connect_pad(0,11,8,0);assert(!mv_controller_poll(&c));
    connect_pad(1,99,8,0);assert(!mv_controller_poll(&c));
    connect_pad(1,99,4,0x20);assert(mv_controller_poll(&c)==(MV_PAD_DOWN|MV_PAD_RB));
    connect_pad(0,11,0,0x10);assert(mv_controller_poll(&c)==(MV_PAD_DOWN|MV_PAD_RB));
    mock[1].count=0;assert(mv_controller_poll(&c)==(MV_PAD_UP|MV_PAD_LB));
    assert(next_calls==0 && acquired==2);mv_controller_close(&c);assert(released==2);
    mv_controller_close(&c);assert(released==2 && !mv_controller_poll(&c));
    mocks_reset();apis[0].struct_size=offsetof(risc_usb_gamepad_api_v1,snapshot);
    apis[1].api_version=2;mv_controller_open(&c,&caps);assert(acquired==2 && released==2);
    assert(!mv_controller_poll(&c));mv_controller_close(&c);assert(released==2);
    t5_provider_capability_api_v1 short_caps=caps;short_caps.struct_size=offsetof(t5_provider_capability_api_v1,release);
    acquired=0;mv_controller_open(&c,&short_caps);assert(!acquired);mv_controller_close(&c);
    mocks_reset();available=0;mv_controller_open(&c,&caps);assert(!mv_controller_poll(&c));mv_controller_close(&c);
    assert(!released);
    puts("providers: current held state, focus, hotplug, failures, ABI bounds, cleanup, no event queue PASS");
}
static mv_motion_delta_t advance(uint32_t held) {
    mv_motion_t m={0};mv_motion_delta_t d;
    assert(!mv_motion_step(&m,held,0,true,&d));
    for(uint32_t ms=8;ms<=160;ms+=8) assert(mv_motion_step(&m,held,ms,true,&d));
    return d;
}
static float travel(unsigned interval) {
    mv_motion_t m={0};mv_motion_delta_t d;float total=0;
    assert(!mv_motion_step(&m,MV_PAD_RIGHT,0,true,&d));
    for(unsigned ms=0;ms<1000;) {
        ms+=interval;if(ms>1000)ms=1000;
        assert(mv_motion_step(&m,MV_PAD_RIGHT,ms,true,&d));total+=d.yaw;
        assert(d.yaw<=3.9f*.048f+1e-7f);
    }
    return total;
}
static void motion_tests(void) {
    mv_motion_delta_t d=advance(MV_PAD_RIGHT);assert(d.yaw>0 && !d.pitch && !d.pan_x && d.zoom_factor==1);
    d=advance(MV_PAD_LEFT);assert(d.yaw<0 && !d.pitch);
    d=advance(MV_PAD_UP);assert(d.pitch<0 && !d.yaw);
    d=advance(MV_PAD_DOWN);assert(d.pitch>0 && !d.yaw);
    d=advance(MV_PAD_LB|MV_PAD_UP);assert(d.zoom_factor>1 && !d.pitch && !d.yaw && !d.pan_y);
    d=advance(MV_PAD_LB|MV_PAD_DOWN);assert(d.zoom_factor<1 && !d.pitch && !d.yaw);
    d=advance(MV_PAD_RB|MV_PAD_RIGHT);assert(d.pan_x>0 && !d.pan_y && !d.yaw && d.zoom_factor==1);
    d=advance(MV_PAD_RB|MV_PAD_LEFT);assert(d.pan_x<0 && !d.yaw);
    d=advance(MV_PAD_RB|MV_PAD_UP);assert(d.pan_y<0 && !d.pitch);
    d=advance(MV_PAD_RB|MV_PAD_DOWN);assert(d.pan_y>0 && !d.pitch);
    d=advance(MV_PAD_RB|MV_PAD_LB|MV_PAD_UP);assert(d.pan_y<0 && d.zoom_factor==1);
    d=advance(MV_PAD_RB|MV_PAD_RIGHT|MV_PAD_DOWN);assert(fabsf(hypotf(d.pan_x,d.pan_y)-23.04f)<5e-6f);
    assert(!mv_motion_action(MV_PAD_LB|MV_PAD_LEFT));assert(!mv_motion_action(MV_PAD_LEFT|MV_PAD_RIGHT));
    assert(!mv_motion_action(MV_PAD_UP|MV_PAD_DOWN));
    assert(fabsf(travel(8)-travel(16))<1e-5f);assert(fabsf(travel(8)-travel(40))<1e-5f);
    assert(fabsf(travel(13)-3.9f*.94f)<1e-5f);
    mv_motion_t m={0};assert(!mv_motion_step(&m,0,0,true,&d));
    assert(!mv_motion_step(&m,MV_PAD_RIGHT,5000,true,&d));
    assert(mv_motion_step(&m,MV_PAD_RIGHT,5008,true,&d));assert(d.yaw<0.002f);
    assert(!mv_motion_step(&m,0,5016,true,&d) && d.yaw==0);
    assert(!mv_motion_step(&m,0,10000,true,&d));
    assert(!mv_motion_step(&m,MV_PAD_RIGHT,10008,true,&d));
    for(unsigned t=10016;t<11000;t+=8) assert(!mv_motion_step(&m,MV_PAD_RIGHT,t,false,&d));
    assert(m.pending_ms==MV_MOTION_MAX_FRAME_MS);
    assert(mv_motion_step(&m,MV_PAD_RIGHT,11000,true,&d));assert(d.yaw<=3.9f*.048f);
    assert(!mv_motion_step(&m,MV_PAD_RB|MV_PAD_UP,11008,true,&d));
    assert(mv_motion_step(&m,MV_PAD_RB|MV_PAD_UP,11016,true,&d) && !d.yaw && d.pan_y<0);
    assert(!mv_motion_step(&m,0,11024,false,&d) && !m.pending_ms);
    assert(!mv_motion_step(&m,MV_PAD_LEFT,11032,true,&d));
    assert(!mv_motion_step(&m,MV_PAD_LEFT,11040,true,&d) && !d.yaw);
    assert(!mv_motion_step(&m,0,11048,true,&d));
    assert(!mv_motion_step(&m,0,11096,true,&d));
    assert(!mv_motion_step(&m,MV_PAD_LEFT,11104,true,&d));
    assert(mv_motion_step(&m,MV_PAD_LEFT,11112,true,&d) && d.yaw<0);
    m=(mv_motion_t){0};assert(!mv_motion_step(&m,MV_PAD_UP,UINT32_MAX-7u,true,&d));
    assert(mv_motion_step(&m,MV_PAD_UP,8,true,&d) && d.pitch<0 && d.pitch>-.02f);
    puts("motion: all requested chords, rate independence, start ramp, release, mode changes, bounded backlog, rollover PASS");
}
static void precision_tests(void) {
    const uint32_t moves[]={MV_PAD_LEFT,MV_PAD_RIGHT,MV_PAD_UP,MV_PAD_DOWN,
        MV_PAD_LB|MV_PAD_UP,MV_PAD_LB|MV_PAD_DOWN,
        MV_PAD_RB|MV_PAD_LEFT,MV_PAD_RB|MV_PAD_RIGHT,MV_PAD_RB|MV_PAD_UP,MV_PAD_RB|MV_PAD_DOWN,
        MV_PAD_RB|MV_PAD_RIGHT|MV_PAD_UP};
    for(size_t i=0;i<sizeof(moves)/sizeof(moves[0]);++i) {
        const mv_motion_delta_t normal=advance(moves[i]),fine=advance(moves[i]|MV_PAD_FINE);
        assert(fabsf(fine.yaw-normal.yaw*.25f)<1e-7f);
        assert(fabsf(fine.pitch-normal.pitch*.25f)<1e-7f);
        assert(fabsf(fine.pan_x-normal.pan_x*.25f)<1e-7f);
        assert(fabsf(fine.pan_y-normal.pan_y*.25f)<1e-7f);
        assert(fabsf(logf(fine.zoom_factor)-logf(normal.zoom_factor)*.25f)<2e-7f);
        mv_motion_t m={0};mv_motion_delta_t d;
        assert(!mv_motion_step(&m,moves[i],0,true,&d));
        for(unsigned ms=8;ms<=160;ms+=8) assert(mv_motion_step(&m,moves[i],ms,true,&d));
        assert(!mv_motion_step(&m,moves[i],168,false,&d));
        assert(!mv_motion_step(&m,moves[i]|MV_PAD_FINE,176,false,&d));
        assert(m.ramp_ms==MV_MOTION_RAMP_MS && !m.pending_ms);
        assert(mv_motion_step(&m,moves[i]|MV_PAD_FINE,184,true,&d));
        assert(fabsf(d.yaw-fine.yaw)<1e-7f && fabsf(d.pan_y-fine.pan_y)<1e-7f);
        assert(!mv_motion_step(&m,moves[i],192,true,&d));
        assert(m.ramp_ms==MV_MOTION_RAMP_MS);
        assert(mv_motion_step(&m,moves[i],200,true,&d));
        assert(fabsf(d.yaw-normal.yaw)<1e-7f && fabsf(d.pan_y-normal.pan_y)<1e-7f);
    }
    assert(!mv_motion_action(MV_PAD_FINE));
    /* Actual 8 ms updates are 2x rotation and 8x pan/zoom versus 1.2.1, not merely labels. */
    assert(fabsf(advance(MV_PAD_RIGHT).yaw - 2.0f*1.95f*.008f)<1e-7f);
    assert(fabsf(advance(MV_PAD_RB|MV_PAD_RIGHT).pan_x - 8.0f*360.0f*.008f)<1e-6f);
    assert(fabsf(logf(advance(MV_PAD_LB|MV_PAD_UP).zoom_factor) - 8.0f*2.25f*.008f)<2e-7f);
    puts("precision: 2x rotation and 8x pan/zoom rates, exact quarter rotation/pan and quarter log-zoom, live A changes PASS");
}
static void integration_tests(void) {
    t5_app_input_t none={0};mv_reset_view();g_motion=(mv_motion_t){0};
    g_controller.held=MV_PAD_RIGHT;ready=false;g_draw_pending=false;
    float yaw=g_view.yaw;assert(mv_buttons(&none,1000));assert(mv_buttons(&none,2000));
    assert(g_view.yaw==yaw && !g_draw_pending);
    ready=true;assert(mv_buttons(&none,2008));assert(g_view.yaw>yaw && g_view.yaw-yaw<.188f && g_draw_pending);
    g_controller.held=0;yaw=g_view.yaw;assert(mv_buttons(&none,2016));assert(mv_buttons(&none,2024));assert(g_view.yaw==yaw);
    g_controller.held=MV_PAD_LB|MV_PAD_UP;g_view.zoom=6.999f;
    for(unsigned t=3000;t<5000;t+=16) { assert(mv_buttons(&none,t)); }
    assert(g_view.zoom==7.0f);
    g_controller.held=MV_PAD_LB|MV_PAD_DOWN;g_view.zoom=.18001f;
    for(unsigned t=5000;t<7000;t+=16) { assert(mv_buttons(&none,t)); }
    assert(g_view.zoom==.18f);
    g_controller.held=MV_PAD_RB|MV_PAD_RIGHT;g_view.pan_x=4095.9f;
    for(unsigned t=7000;t<8000;t+=16) { assert(mv_buttons(&none,t)); }
    assert(g_view.pan_x==4096);
    const mv_view_t saved=g_view;
    g_controller.held=MV_PAD_FINE;assert(mv_buttons(&none,8016));assert(!memcmp(&saved,&g_view,sizeof(saved)));
    g_controller.held=0;assert(mv_buttons(&none,8032));assert(!memcmp(&saved,&g_view,sizeof(saved)));
    g_controller.held=MV_PAD_BACK;assert(!mv_buttons(&none,8048));
    g_motion=(mv_motion_t){0};
    mocks_reset();mv_controller_open(&g_controller,&caps);connect_pad(0,10,8,0);mv_controller_poll(&g_controller);
    connect_pad(0,10,2,0);g_render_service_ms=0;clock_ms=16;g_render_interactive=true;
    assert(mv_render_service()); /* held rotation must not starve interactive frames */
    clock_ms=32;g_render_interactive=false;g_draw_pending=false;
    assert(!mv_render_service() && g_draw_pending); /* interrupt full refinement */
    clock_ms=48;g_render_interactive=true;assert(mv_render_service());
    connect_pad(0,10,8,0);clock_ms=64;assert(mv_render_service() && !g_motion.action && !g_motion.pending_ms);
    connect_pad(0,10,8,2);clock_ms=80;assert(!mv_render_service() && g_render_input_pending && g_render_input.exit_requested);
    mv_controller_close(&g_controller);
    puts("integration: dirty redraws while held, frame backpressure, zoom/pan bounds, A no reset, render servicing/exit PASS");
}
static void arbitration_tests(void) {
    /* Feed BOTH the real raw snapshot and its legacy direction projection.
     * The old mv_buttons applies 0.012 rad rotation even in RB pan / LB zoom. */
    const uint8_t hats[]={0,2,4,6};
    const uint32_t directions[]={T5_APP_BUTTON_UP,T5_APP_BUTTON_RIGHT,T5_APP_BUTTON_DOWN,T5_APP_BUTTON_LEFT};
    for(unsigned provider=0;provider<2;++provider) for(unsigned fine=0;fine<2;++fine) {
        for(unsigned mode=0;mode<2;++mode) for(unsigned axis=0;axis<4;++axis) {
            if(mode==1 && (axis==1 || axis==3)) continue;
            mocks_reset();mv_controller_open(&g_controller,&caps);
            connect_pad(provider,42,8,0);mv_controller_poll(&g_controller);
            const uint32_t buttons=(mode?0x10u:0x20u)|(fine?(provider?2u:1u):0u);
            connect_pad(provider,42,hats[axis],buttons);
            mv_reset_view();g_motion=(mv_motion_t){0};g_render_input_pending=false;
            g_view.yaw=.9f;g_view.pitch=.7f;g_view.zoom=2;g_view.pan_x=7;g_view.pan_y=9;
            for(unsigned n=0;n<25;++n) {
                mv_controller_poll(&g_controller);
                input_state=(t5_app_input_t){.buttons=directions[axis]|(fine?T5_APP_BUTTON_CONFIRM:0u)};
                assert(mv_buttons(&input_state,n*16u));
                assert(g_view.yaw==.9f && g_view.pitch==.7f);
                clock_ms=n*16u+8u;g_render_service_ms=clock_ms-16u;g_render_interactive=true;
                assert(mv_render_service() && !g_render_input_pending);
            }
            if(mode) assert(g_view.zoom!=2 && g_view.pan_x==7 && g_view.pan_y==9);
            else assert(g_view.zoom==2 && (g_view.pan_x!=7 || g_view.pan_y!=9));
            /* Fault/rearm cannot hand raw input back to the rotation fallback. */
            const mv_view_t before=g_view;
            mock[provider].poll_ok=false;mv_controller_poll(&g_controller);
            assert(mv_buttons(&input_state,500));assert(!memcmp(&before,&g_view,sizeof(before)));
            mock[provider].poll_ok=true;mv_controller_poll(&g_controller);
            assert(mv_buttons(&input_state,516));assert(!memcmp(&before,&g_view,sizeof(before)));
            /* Unplug with projected state still down: drain it, do not rotate. */
            mock[provider].count=0;mv_controller_poll(&g_controller);
            assert(mv_buttons(&input_state,532));assert(!memcmp(&before,&g_view,sizeof(before)));
            input_state=(t5_app_input_t){0};assert(mv_buttons(&input_state,548));
            input_state.buttons=T5_APP_BUTTON_RIGHT;assert(mv_buttons(&input_state,564));
            assert(g_view.yaw>before.yaw); /* Keyboard/physical fallback rearmed. */
            input_state.buttons=T5_APP_BUTTON_CONFIRM;assert(mv_buttons(&input_state,580));
            assert(g_view.zoom==1 && g_view.pan_x==0);
            input_state.buttons=T5_APP_BUTTON_BACK;assert(!mv_buttons(&input_state,596));
            mv_controller_close(&g_controller);
        }
    }
    input_state=(t5_app_input_t){0};
    puts("arbitration: HID/XInput pan/zoom plus duplicate directions/A, faults, unplug neutral handback, Back PASS");
}
/* Exercise the unchanged app handler and its render checkpoint with the
 * provider's current snapshot AND duplicate mapped input. The previous suite
 * kept the bumper permanently set, so it never tested the release-order hole. */
static void gesture_sample(unsigned provider, uint8_t hat, uint32_t buttons, uint32_t ms) {
    connect_pad(provider,42,hat,buttons);
    mv_controller_poll(&g_controller);
    input_state=(t5_app_input_t){.buttons=MV_MAPPED_CAMERA_BUTTONS};
    assert(mv_buttons(&input_state,ms));
    clock_ms=ms+4u;g_render_service_ms=clock_ms-16u;g_render_interactive=true;
    assert(mv_render_service() && !g_render_input_pending);
}
static void exclusive_gesture_tests(void) {
    const uint8_t hats[]={0,2,4,6};
    for(unsigned provider=0;provider<2;++provider) for(unsigned fine=0;fine<2;++fine)
    for(unsigned mode=0;mode<2;++mode) for(unsigned axis=0;axis<4;++axis) {
        if(mode && (axis==1 || axis==3)) continue;
        mocks_reset();mv_controller_open(&g_controller,&caps);
        connect_pad(provider,42,8,0);mv_controller_poll(&g_controller);
        g_motion=(mv_motion_t){0};g_render_input_pending=false;ready=true;
        mv_reset_view();g_view.yaw=.8f;g_view.pitch=.6f;
        const uint32_t a=fine?(provider?2u:1u):0u;
        const uint32_t bumper=mode?0x10u:0x20u;
        gesture_sample(provider,8,bumper|a,1000); /* Bumper before direction. */
        for(unsigned ms=1008;ms<=1160;ms+=8) gesture_sample(provider,hats[axis],bumper|a,ms);
        assert(g_view.yaw==.8f && g_view.pitch==.6f);
        assert(g_motion.ramp_ms==120u);
        mv_view_t saved=g_view;
        /* Bumper disappears while D-pad remains down, even changes direction.
         * Neither raw rotation nor mapped rotation/reset may slip through. */
        for(unsigned ms=1168;ms<=1328;ms+=8) {
            gesture_sample(provider,ms<1248?hats[axis]:hats[(axis+2u)%4u],a,ms);
            assert(!memcmp(&saved,&g_view,sizeof(saved)) && !g_motion.pending_ms);
        }
        assert(g_motion.ramp_ms==120u); /* No slow re-ramp on a lost modifier. */
        g_view.zoom=1;g_view.pan_x=g_view.pan_y=0;
        gesture_sample(provider,hats[axis],bumper|a,1336);
        assert(g_view.yaw==.8f && g_view.pitch==.6f);
        if(mode) assert(g_view.zoom!=1 && !g_view.pan_x && !g_view.pan_y);
        else assert(g_view.zoom==1 && (g_view.pan_x || g_view.pan_y));
        /* Releasing direction first but retaining the bumper keeps ownership. */
        gesture_sample(provider,8,bumper|a,1344);
        saved=g_view;
        gesture_sample(provider,2,a,1352);
        gesture_sample(provider,2,a,1360);
        assert(!memcmp(&saved,&g_view,sizeof(saved)));
        /* One brief fully-neutral report must not unlock rotation. */
        gesture_sample(provider,8,a,1368);
        gesture_sample(provider,2,a,1376);
        gesture_sample(provider,2,a,1440);
        assert(!memcmp(&saved,&g_view,sizeof(saved)));
        /* A can stay held through the neutral rearm. */
        gesture_sample(provider,8,a,1448);
        gesture_sample(provider,8,a,1496);
        gesture_sample(provider,2,a,1504);
        gesture_sample(provider,2,a,1512);
        assert(g_view.yaw>saved.yaw && g_view.pitch==saved.pitch);
        /* Explicit mode change is allowed without neutral, never via rotate. */
        gesture_sample(provider,0,0x10u|a,1520);
        gesture_sample(provider,0,0x10u|a,1528);
        const float yaw=g_view.yaw,pitch=g_view.pitch;
        gesture_sample(provider,0,0x30u|a,1536); /* Both: RB wins. */
        gesture_sample(provider,0,0x30u|a,1544);
        assert(g_view.yaw==yaw && g_view.pitch==pitch && g_motion.bumper_mode==MV_PAD_RB);
        input_state=(t5_app_input_t){.buttons=T5_APP_BUTTON_BACK};
        assert(!mv_buttons(&input_state,1552));
        mv_controller_close(&g_controller);
    }
    /* Time-based neutral rearming must also work across uint32_t rollover. */
    mv_motion_t m={0};mv_motion_delta_t d;
    assert(!mv_motion_step(&m,MV_PAD_RB,UINT32_MAX-64u,true,&d));
    assert(!mv_motion_step(&m,0,UINT32_MAX-16u,true,&d));
    assert(!mv_motion_step(&m,0,31u,true,&d));
    assert(!mv_motion_step(&m,MV_PAD_RIGHT,39u,true,&d));
    assert(mv_motion_step(&m,MV_PAD_RIGHT,47u,true,&d) && d.yaw>0);
    input_state=(t5_app_input_t){0};
    puts("gestures: missing bumpers, both release orders, neutral blips, resume, A, live mode changes and rollover PASS");
}
static void fast_zoom_accuracy_tests(void) {
    /* The old degree-4 expression at z=-.864 is measurably asymmetric.
     * Verify the entire new operating range, not just a single small frame. */
    for(unsigned i=0;i<=2000;++i) {
        const float z=-.864f+1.728f*(float)i/2000.0f;
        const float actual=mv_zoom_factor(z);
        assert(actual>0 && fabsf(actual/expf(z)-1.0f)<0.000002f);
    }
    for(unsigned ms=1;ms<=48;++ms) {
        const float z=18.0f*(float)ms*.001f;
        assert(fabsf(logf(mv_zoom_factor(z*.25f))-.25f*logf(mv_zoom_factor(z)))<0.000001f);
        assert(fabsf(mv_zoom_factor(z)*mv_zoom_factor(-z)-1.0f)<0.000002f);
    }
    puts("zoom: accurate 8x rate across 1..48 ms frames, reciprocal zoom and quarter-speed logarithmic precision PASS");
}
int main(void) { mapping_tests();provider_tests();motion_tests();precision_tests();integration_tests();arbitration_tests();exclusive_gesture_tests();fast_zoom_accuracy_tests();return 0; }
'''


class ControllerTests(unittest.TestCase):
    def test_production_code(self):
        cc = shutil.which(os.environ.get("CC", "cc"))
        self.assertIsNotNone(cc, "A host C compiler is required")
        camera_mask = re.search(r"^#define MV_MAPPED_CAMERA_BUTTONS .+$", APP, re.M).group(0)
        source = HARNESS + camera_mask + "\n" + "\n".join(function(n) for n in
            ["mv_clampf", "mv_wrap_angle", "mv_reset_view", "mv_buttons", "mv_render_service"]) + TESTS
        with tempfile.TemporaryDirectory(prefix="model-viewer-controls-") as tmp:
            cfile, binary = Path(tmp) / "test.c", Path(tmp) / "test"
            cfile.write_text(source)
            command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(ROOT / "Apps"), "-I" + str(ROOT / "sdk/driver"),
                       "-I" + str(ROOT / "lib/NativeApps/include"), str(cfile), "-lm", "-o", str(binary)]
            if os.environ.get("MV_SANITIZE") == "1":
                command[2:2] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)

    def test_render_and_manifest_integration(self):
        manifest = json.loads((ROOT / "Apps/model_viewer.json").read_text())
        self.assertGreaterEqual(tuple(map(int, manifest["version"].split("."))), (1, 2, 4))
        for capability in ["usb.hid.gamepad", "usb.xinput.gamepad"]:
            self.assertIn({"capability": capability, "api": ">=1"}, manifest["optional"])
        self.assertIn("g_draw_pending && mv_render(true)", APP)
        self.assertIn("!mv_motion_action(g_controller.held)", APP)
        self.assertIn("mv_controller_open(&g_controller,g_caps)", APP)
        self.assertIn("mv_controller_close(&g_controller)", APP)
        self.assertNotIn("g_view.yaw+0.08f", APP)
        header = (ROOT / "Apps/model_viewer_controls.h").read_text()
        self.assertNotIn("->next(", header)
        self.assertNotIn("usb_host_", header)


if __name__ == "__main__":
    unittest.main()