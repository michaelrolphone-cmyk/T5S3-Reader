#!/usr/bin/env python3
"""Execute the production mesh renderer, preview cache and resampler on the host.

Only display publication, allocation, clock, cooperative cancellation and the
math-provider service are substituted. No replacement rasterizer is used.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = (ROOT / "Apps/model_viewer.c").read_text()
CONTROLS = (ROOT / "Apps/model_viewer_controls.h").read_text()


def function(source, name):
    match = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\(", source, re.M)
    if not match:
        raise AssertionError(f"Missing production function: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


PREAMBLE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model_viewer_preview.h"
#define T5_FILE_OPEN_PATH_MAX 512
#define T5_APP_BUTTON_BACK 1u
#define T5_APP_BUTTON_CONFIRM 2u
#define T5_APP_BUTTON_LEFT 4u
#define T5_APP_BUTTON_RIGHT 8u
#define T5_APP_BUTTON_UP 16u
#define T5_APP_BUTTON_DOWN 32u
typedef int t5_storage_stream_t;
'''

SERVICES = r'''
static mv_model_t g_model;
static mv_view_t g_view;
static mv_preview_t g_preview;
static bool g_use_psram=true,g_shaded,g_render_interactive;
static char g_path[T5_FILE_OPEN_PATH_MAX]="test.obj",g_status[96];
static uint16_t depth[MV_DEPTH_COUNT];
static uint16_t *g_depth=depth;
static uint32_t g_render_service_ms;
static struct { uint32_t held; } g_controller;
static struct { uint16_t stride_bytes,height; } g_surface={120,540};
static uint8_t frame[64800];
static bool ready=true,accept=true,fail_alloc;
static unsigned allocs,frees,submits,transforms,service_calls,cancel_at;
static bool accelerated;
static uint8_t *allocation;
static void *allocate(size_t n) {
    assert(n==MV_PREVIEW_BYTES);++allocs;
    if(fail_alloc) return NULL;
    assert(!allocation);allocation=malloc(n+32);assert(allocation);
    memset(allocation,0xa5,n+32);return allocation+16;
}
static void deallocate(void *ptr) {
    assert(ptr==allocation+16);
    for(unsigned i=0;i<16;++i) assert(allocation[i]==0xa5 && allocation[16+MV_PREVIEW_BYTES+i]==0xa5);
    free(allocation);allocation=NULL;++frees;
}
static uint32_t now(void) { return 100; }
static bool can_submit(void) { return ready; }
static uint8_t *backbuffer(size_t *size) { *size=sizeof(frame);return frame; }
static bool submit(uint16_t row,uint16_t height) { assert(row==0 && height==540);++submits;return accept; }
static bool mv_render_service(void) { return !cancel_at || ++service_calls<cancel_at; }
static bool mat4(const float *in,const float *m,float *out,size_t count) {
    ++transforms;
    if(!accelerated) return false;
    for(size_t v=0;v<count;++v) for(unsigned r=0;r<4;++r) {
        float n=0;for(unsigned c=0;c<4;++c)n+=m[c*4+r]*in[v*4+c];out[v*4+r]=n;
    }
    return true;
}
static bool dot4(const float *a,const float *b,float *out,size_t n) {
    for(size_t i=0;i<n;++i) out[i]=a[i*4]*b[i*4]+a[i*4+1]*b[i*4+1]+a[i*4+2]*b[i*4+2]+a[i*4+3]*b[i*4+3];
    return true;
}
static const struct { void *(*psram_alloc)(size_t);void (*psram_free)(void *);uint32_t (*millis)(void); }
    app={allocate,deallocate,now},*g_app=&app;
static const struct { bool (*can_submit)(void);uint8_t *(*backbuffer)(size_t *);bool (*submit)(uint16_t,uint16_t); }
    video={can_submit,backbuffer,submit},*g_video=&video;
static const struct { bool (*mat4_f32)(const float *,const float *,float *,size_t);
                     bool (*dot4_f32)(const float *,const float *,float *,size_t); }
    math_service={mat4,dot4},*g_math=&math_service;
typedef struct { float cy,sy,cp,sp; } mv_rotation_t;
'''

TESTS = r'''
static const mv_triangle_t square[]={
    {{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0}},
    {{-.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}}
};
static bool black_at(int x,int y) { return (frame[(539-x)*120+(y>>3)]&(0x80u>>(y&7)))!=0; }
static unsigned ink_in(int x0,int y0,int w,int h) {
    unsigned sum=0;for(int y=y0;y<y0+h;++y)for(int x=x0;x<x0+w;++x)sum+=black_at(x,y);return sum;
}
static void reset_scene(void) {
    mv_preview_release();
    g_model=(mv_model_t){.triangles=(mv_triangle_t *)square,.triangle_count=2,.normalize=1};
    g_view=(mv_view_t){.zoom=1};g_controller.held=MV_PAD_RB|MV_PAD_RIGHT;
    g_shaded=true;g_use_psram=true;g_status[0]=0;
    service_calls=cancel_at=0;ready=accept=true;fail_alloc=false;
}
static void cache_tests(void) {
    for(unsigned math_path=0;math_path<2;++math_path) for(unsigned shade=0;shade<2;++shade) {
        reset_scene();accelerated=math_path;g_shaded=shade;
        unsigned before=transforms;
        assert(mv_render(true) && g_preview.valid && transforms==before+1);
        const unsigned built=transforms,allocations=allocs;
        uint16_t old_depth[MV_DEPTH_COUNT];memcpy(old_depth,depth,sizeof(depth));
        for(unsigned n=1;n<=40;++n) {
            g_view.pan_x=(float)n*.25f;g_view.pan_y=-(float)n*.125f;
            g_view.zoom=1.0f+(float)n*.025f;
            g_controller.held=(n&1?MV_PAD_RB|MV_PAD_UP:MV_PAD_LB|MV_PAD_UP)|MV_PAD_FINE;
            assert(mv_render(true) && transforms==built && allocs==allocations);
        }
        assert(!memcmp(depth,old_depth,sizeof(depth))); /* no repeated depth clearing/testing */
        g_view.yaw=.2f;assert(mv_render(true) && transforms==built+1);
        g_view.pitch=.1f;assert(mv_render(true) && transforms==built+2);
        g_shaded=!g_shaded;assert(mv_render(true) && transforms==built+3);
        before=transforms;g_controller.held=MV_PAD_RIGHT;
        assert(mv_render(true) && transforms==before+1); /* rotation uses exact mesh path */
        before=transforms;assert(mv_render(false) && transforms==before+1); /* idle refinement */
        mv_preview_release();assert(!g_preview.ink && !g_preview.valid);
    }
    puts("cache: shaded/wire + scalar/accelerated; 40 changed pan/zoom frames use zero mesh/depth passes PASS");
}
static void framing_tests(void) {
    reset_scene();
    /* Build while the whole object is offscreen at maximum zoom. Bringing it
       back must reveal the complete model, not an empty cropped screenshot. */
    g_view.pan_x=4096;g_view.zoom=7;
    assert(mv_render(true) && ink_in(4,78,532,799)==0);
    unsigned before=transforms;g_view.pan_x=0;g_view.zoom=1;
    assert(mv_render(true) && transforms==before && ink_in(100,300,320,320)>10000);
    g_view.pan_x=-4096;assert(mv_render(true) && ink_in(4,78,532,799)==0);
    g_view.pan_x=0;g_view.pan_y=4096;assert(mv_render(true) && ink_in(4,78,532,799)==0);
    g_view.pan_y=0;
    /* Exact renderer is unchanged: idle result must not depend on the cache. */
    assert(mv_render(false));uint8_t exact[64800];memcpy(exact,frame,sizeof(frame));
    g_view.zoom=6;assert(mv_render(true));g_view.zoom=1;
    assert(mv_render(false) && !memcmp(frame,exact,sizeof(frame)));
    for(unsigned row=0;row<MV_PREVIEW_SIDE;++row) {
        assert(g_preview.ink[row*MV_PREVIEW_SIDE]==0);
        assert(g_preview.ink[row*MV_PREVIEW_SIDE+MV_PREVIEW_SIDE-1]==0);
    }
    puts("framing: offscreen recovery, complete object, cleared background, exact idle redraw PASS");
}
static void filtering_tests(void) {
    reset_scene();assert(mv_render(true));
    uint8_t samples[]={0,128,128,255};
    assert(mv_preview_sample(samples,2,0,0)==0);
    assert(mv_preview_sample(samples,2,16384,0)==32);
    assert(mv_preview_sample(samples,2,32768,0)==64);
    assert(mv_preview_sample(samples,2,49152,0)==96);
    assert(mv_preview_sample(samples,2,32768,32768)==128);
    assert(mv_preview_sample(samples,2,-1,0)==0);
    assert(mv_preview_sample(samples,2,65536,0)==0);
    /* Filter actual wire coverage when downsampling rather than dropping it. */
    memset(g_preview.ink,0,MV_PREVIEW_BYTES);
    for(unsigned y=0;y<MV_PREVIEW_SIDE;++y)g_preview.ink[y*MV_PREVIEW_SIDE+300]=255;
    assert(mv_preview_finish(&g_preview,0,0,false,NULL));
    const size_t l1=640u*640u,l2=l1+320u*320u;
    assert(g_preview.ink[l1+150]==128 && g_preview.ink[l2+75]==64);
    /* A flat face retains the identical screen-anchored dither even while its
       source moves/scales. Fractional positions must not scale a hatch texture. */
    for(unsigned d=2;d<=14;++d) {
        memset(g_preview.ink,d*16u,MV_PREVIEW_BYTES);
        for(unsigned n=0;n<3;++n) {
            memset(frame,0,sizeof(frame));
            assert(mv_preview_draw(&g_preview,frame,mv_pixel,100,300,100,100,
                150.0f+n*.37f,350.0f-n*.31f,300.0f+n*20.0f,NULL));
            for(int y=300;y<400;++y)for(int x=100;x<200;++x)
                assert(black_at(x,y)==mv_shade_black(x,y,d));
            assert(!black_at(99,300) && !black_at(200,300) && !black_at(100,299));
        }
    }
    /* Level changes blend to the same coarse image on both sides, rather
       than snapping between independent nearest-mip samples. */
    for(unsigned y=0;y<MV_PREVIEW_SIDE;++y)for(unsigned x=0;x<MV_PREVIEW_SIDE;++x)
        g_preview.ink[y*MV_PREVIEW_SIDE+x]=((x/3+y/3)&1u)?224u:32u;
    assert(mv_preview_finish(&g_preview,0,0,false,NULL));
    for(unsigned boundary=2;boundary<=4;boundary+=2) {
        uint8_t prior[64800];memset(frame,0,sizeof(frame));
        assert(mv_preview_draw(&g_preview,frame,mv_pixel,100,300,100,100,150,350,
            319.0f/((float)boundary-.0001f),NULL));
        memcpy(prior,frame,sizeof(frame));memset(frame,0,sizeof(frame));
        assert(mv_preview_draw(&g_preview,frame,mv_pixel,100,300,100,100,150,350,
            319.0f/((float)boundary+.0001f),NULL));
        unsigned changed=0;
        for(size_t i=0;i<sizeof(frame);++i)for(unsigned b=0;b<8;++b)
            changed+=((frame[i]^prior[i])>>b)&1u;
        assert(changed<150); /* <1.5% of viewport across either LOD boundary */
    }
    puts("filter: fractional interpolation, thin-wire mip coverage, stable unscaled dither PASS");
}
static void depth_tests(void) {
    reset_scene();assert(mv_render(true));
    mv_shade_surface_t s;
    const mv_shade_vertex_t a={10,10,-.5f},b={100,10,-.5f},c={10,100,-.5f};
    mv_shade_vertex_t na=a,nb=b,nc=c;na.z=nb.z=nc.z=.5f;
    for(unsigned order=0;order<2;++order) {
        assert(mv_preview_clear(&g_preview,NULL));
        assert(mv_shade_begin(&s,depth,MV_DEPTH_COUNT,0,0,640,640,1,NULL));
        if(order)assert(mv_preview_triangle(&s,g_preview.ink,na,nb,nc,2));
        assert(mv_preview_triangle(&s,g_preview.ink,a,b,c,14));
        if(!order)assert(mv_preview_triangle(&s,g_preview.ink,na,nb,nc,2));
        for(int y=20;y<30;++y)for(int x=20;x<30;++x)assert(g_preview.ink[y*640+x]==32);
    }
    puts("depth: foreground ink replaces dark rear faces in both draw orders PASS");
}
static void safety_tests(void) {
    reset_scene();unsigned before=allocs;fail_alloc=true;
    assert(mv_render(true) && g_preview.unavailable && !g_preview.ink && allocs==before+1);
    assert(mv_render(true) && allocs==before+1); /* fallback, not a retry loop */
    reset_scene();g_use_psram=false;before=allocs;
    assert(mv_render(true) && !g_preview.ink && allocs==before);
    reset_scene();ready=false;before=allocs;
    assert(!mv_render(true) && allocs==before);
    ready=true;cancel_at=5;service_calls=0;unsigned published=submits;
    assert(!mv_render(true) && !g_preview.valid && submits==published);
    cancel_at=0;assert(mv_render(true) && g_preview.valid);
    before=transforms;cancel_at=3;service_calls=0;published=submits;
    assert(!mv_render(true) && g_preview.valid && submits==published && transforms==before);
    cancel_at=0;accept=false;assert(!mv_render(true) && transforms==before);
    accept=true;assert(mv_render(true) && transforms==before);
    assert(!mv_preview_draw(&g_preview,frame,mv_pixel,4,78,532,799,NAN,468,390,NULL));
    assert(!mv_preview_draw(&g_preview,frame,mv_pixel,4,78,532,799,270,468,0,NULL));
    assert(!mv_preview_draw(&g_preview,frame,mv_pixel,4,78,532,799,270,468,INFINITY,NULL));
    /* Maximum public argument bounds, with cancellation after a few rows. */
    cancel_at=2;service_calls=0;
    assert(!mv_preview_draw(&g_preview,frame,mv_pixel,4096,4096,4096,4096,-16384,-16384,64,mv_render_service));
    cancel_at=0;
    mv_preview_release();unsigned released=frees;mv_preview_release();assert(frees==released);
    assert(!allocation);
    puts("safety: PSRAM failure fallback, bounds, cancellation, busy/rejected frames, cleanup PASS");
}
int main(void) { cache_tests();framing_tests();filtering_tests();depth_tests();safety_tests();return 0; }
'''


def translation_unit():
    app_constants = "\n".join(re.findall(r"^#define MV_[^\n]+", APP, re.M))
    pad_constants = "\n".join(re.findall(r"^#define MV_PAD_[^\n]+", CONTROLS, re.M))
    types = APP[APP.index("typedef struct { float x, y, z; }"):APP.index("static const t5_app_api_v1")]
    names = ["mv_iabs", "mv_wrap_angle", "mv_sin", "mv_cos", "mv_alloc", "mv_free", "mv_pixel",
             "mv_glyph", "mv_text_ink", "mv_text", "mv_shade_button_draw", "mv_clip_code",
             "mv_clip_line", "mv_line", "mv_camera_point", "mv_screen_point", "mv_basename",
             "mv_render_step", "mv_preview_release", "mv_render_mesh", "mv_render"]
    clip_enum = re.search(r"enum \{ MV_CLIP_LEFT[^\n]+", APP).group(0)
    return (PREAMBLE + app_constants + "\n" + pad_constants + "\n" + types + SERVICES +
            function(CONTROLS, "mv_motion_action") + clip_enum + "\n" +
            "\n".join(function(APP, n) for n in names) + TESTS)


class PreviewTests(unittest.TestCase):
    def test_production_rendering(self):
        compiler = shutil.which(os.environ.get("CC", "cc"))
        self.assertIsNotNone(compiler, "A host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="model-viewer-preview-") as temp:
            source, binary = Path(temp) / "test.c", Path(temp) / "test"
            source.write_text(translation_unit())
            command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-function", "-I" + str(ROOT / "Apps"), str(source), "-o", str(binary)]
            if os.environ.get("MV_SANITIZE") == "1":
                command[2:2] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)

    def test_lifecycle_and_speed_integration(self):
        self.assertIn("mv_preview_release();\n    mv_shade_release();", APP)
        self.assertIn("memset(&g_preview,0,sizeof(g_preview));", APP)
        for definition in ["MV_ROTATE_RADIANS_PER_SEC 3.90f", "MV_PAN_PIXELS_PER_SEC 2880.0f",
                           "MV_ZOOM_RATE_PER_SEC 18.0f", "MV_FINE_SPEED_SCALE 0.25f"]:
            self.assertIn(definition, CONTROLS)
        self.assertIn("!mv_motion_action(g_controller.held)", APP)
        self.assertIn("mv_render(false)", APP)


if __name__ == "__main__":
    unittest.main()
