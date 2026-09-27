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
static uint32_t g_previous_pad_buttons,g_last_interaction_ms,g_render_service_ms;
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
    assert(mv_pad_decode(&p,false)==MV_PAD_CONFIRM);assert(mv_pad_decode(&p,true)==MV_PAD_BACK);
    p.buttons=2;assert(mv_pad_decode(&p,false)==MV_PAD_BACK);assert(mv_pad_decode(&p,true)==MV_PAD_CONFIRM);
    p.connected=0;assert(!mv_pad_decode(&p,false));
    puts("mapping: hats, diagonals, SNES axis D-pad, shoulders, HID/XInput Confirm/Back PASS");
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
        assert(d.yaw<=0.65f*.048f+1e-7f);
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
    d=advance(MV_PAD_RB|MV_PAD_RIGHT|MV_PAD_DOWN);assert(fabsf(d.pan_x*d.pan_x+d.pan_y*d.pan_y-(.96f*.96f))<1e-5f);
    assert(!mv_motion_action(MV_PAD_LB|MV_PAD_LEFT));assert(!mv_motion_action(MV_PAD_LEFT|MV_PAD_RIGHT));
    assert(!mv_motion_action(MV_PAD_UP|MV_PAD_DOWN));
    assert(fabsf(travel(8)-travel(16))<1e-5f);assert(fabsf(travel(8)-travel(40))<1e-5f);
    assert(fabsf(travel(13)-.65f*.94f)<1e-5f);
    mv_motion_t m={0};assert(!mv_motion_step(&m,0,0,true,&d));
    assert(!mv_motion_step(&m,MV_PAD_RIGHT,5000,true,&d));
    assert(mv_motion_step(&m,MV_PAD_RIGHT,5008,true,&d));assert(d.yaw<0.001f);
    assert(!mv_motion_step(&m,0,5016,true,&d) && d.yaw==0);
    assert(!mv_motion_step(&m,0,10000,true,&d));
    assert(!mv_motion_step(&m,MV_PAD_RIGHT,10008,true,&d));
    for(unsigned t=10016;t<11000;t+=8) assert(!mv_motion_step(&m,MV_PAD_RIGHT,t,false,&d));
    assert(m.pending_ms==MV_MOTION_MAX_FRAME_MS);
    assert(mv_motion_step(&m,MV_PAD_RIGHT,11000,true,&d));assert(d.yaw<=.65f*.048f);
    assert(!mv_motion_step(&m,MV_PAD_RB|MV_PAD_UP,11008,true,&d));
    assert(mv_motion_step(&m,MV_PAD_RB|MV_PAD_UP,11016,true,&d) && !d.yaw && d.pan_y<0);
    assert(!mv_motion_step(&m,0,11024,false,&d) && !m.pending_ms);
    assert(!mv_motion_step(&m,MV_PAD_LEFT,11032,true,&d));
    assert(mv_motion_step(&m,MV_PAD_LEFT,11040,true,&d) && d.yaw<0);
    m=(mv_motion_t){0};assert(!mv_motion_step(&m,MV_PAD_UP,UINT32_MAX-7u,true,&d));
    assert(mv_motion_step(&m,MV_PAD_UP,8,true,&d) && d.pitch<0 && d.pitch>-.02f);
    puts("motion: all requested chords, rate independence, start ramp, release, mode changes, bounded backlog, rollover PASS");
}
static void integration_tests(void) {
    t5_app_input_t none={0};mv_reset_view();g_motion=(mv_motion_t){0};
    g_controller.held=MV_PAD_RIGHT;ready=false;g_draw_pending=false;
    float yaw=g_view.yaw;assert(mv_buttons(&none,1000));assert(mv_buttons(&none,2000));
    assert(g_view.yaw==yaw && !g_draw_pending);
    ready=true;assert(mv_buttons(&none,2008));assert(g_view.yaw>yaw && g_view.yaw-yaw<.032f && g_draw_pending);
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
    g_controller.held=MV_PAD_CONFIRM;assert(mv_buttons(&none,8016));assert(g_view.zoom==1 && g_view.pan_x==0);
    g_view.pan_x=123;assert(mv_buttons(&none,8032));assert(g_view.pan_x==123); /* once per press */
    g_controller.held=MV_PAD_BACK;assert(!mv_buttons(&none,8048));
    mocks_reset();mv_controller_open(&g_controller,&caps);connect_pad(0,10,8,0);mv_controller_poll(&g_controller);
    connect_pad(0,10,2,0);g_render_service_ms=0;clock_ms=16;g_render_interactive=true;
    assert(mv_render_service()); /* held rotation must not starve interactive frames */
    clock_ms=32;g_render_interactive=false;g_draw_pending=false;
    assert(!mv_render_service() && g_draw_pending); /* interrupt full refinement */
    clock_ms=48;g_render_interactive=true;assert(mv_render_service());
    connect_pad(0,10,8,0);clock_ms=64;assert(mv_render_service() && !g_motion.action && !g_motion.pending_ms);
    connect_pad(0,10,8,2);clock_ms=80;assert(!mv_render_service() && g_render_input_pending && g_render_input.exit_requested);
    mv_controller_close(&g_controller);
    puts("integration: dirty redraws while held, frame backpressure, zoom/pan bounds, edge reset, render servicing/exit PASS");
}
int main(void) { mapping_tests();provider_tests();motion_tests();integration_tests();return 0; }
'''


class ControllerTests(unittest.TestCase):
    def test_production_code(self):
        cc = shutil.which(os.environ.get("CC", "cc"))
        self.assertIsNotNone(cc, "A host C compiler is required")
        source = HARNESS + "\n".join(function(n) for n in
            ["mv_clampf", "mv_wrap_angle", "mv_reset_view", "mv_buttons", "mv_render_service"]) + TESTS
        with tempfile.TemporaryDirectory(prefix="model-viewer-controls-") as tmp:
            cfile, binary = Path(tmp) / "test.c", Path(tmp) / "test"
            cfile.write_text(source)
            command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(ROOT / "Apps"), "-I" + str(ROOT / "sdk/driver"),
                       "-I" + str(ROOT / "lib/NativeApps/include"), str(cfile), "-o", str(binary)]
            if os.environ.get("MV_SANITIZE") == "1":
                command[2:2] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)

    def test_render_and_manifest_integration(self):
        manifest = json.loads((ROOT / "Apps/model_viewer.json").read_text())
        self.assertEqual(manifest["version"], "1.2.0")
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
