#!/usr/bin/env python3
"""Execute the real software rasterizer and viewer gesture helpers on the host.

No mock renderer: compile model_viewer_shading.h and extract the unmodified
app functions under test. Only allocation, the video surface and app services
are host substitutes. The existing native-ELF CI validates the complete app ABI.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = (ROOT / 'Apps/model_viewer.c').read_text(encoding='utf-8')


def function(name):
    match = re.search(r'^static [^\n]*\b' + re.escape(name) + r'\(', APP, re.M)
    if not match:
        raise AssertionError(f'Missing production function: {name}')
    end = APP.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (APP[end] == '{') - (APP[end] == '}')
        end += 1
    return APP[match.start():end] + '\n'


HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model_viewer_shading.h"
#define T5_FILE_OPEN_PATH_MAX 512
#define T5_APP_BUTTON_BACK 1u
#define T5_APP_BUTTON_RIGHT 8u
typedef int t5_storage_stream_t;
typedef struct { uint32_t buttons; bool tapped; int16_t touch_x,touch_y; bool exit_requested; } t5_app_input_t;
static struct { uint32_t stride_bytes; } g_surface={120};
static struct { void *(*psram_alloc)(size_t); void (*psram_free)(void *); uint32_t (*millis)(void); bool (*poll)(t5_app_input_t *,uint32_t); } app_services;
static const __typeof__(app_services) *g_app=&app_services;
'''

TESTS = r'''
#define W 96
#define H 80
static unsigned writes;
static void pixel(uint8_t *p,int x,int y,bool black) {
    assert(x>=4 && x<65 && y>=8 && y<59);
    p[y*W+x]=(uint8_t)black;
    ++writes;
}
static void begin(mv_shade_surface_t *s,uint16_t *depth,int sample) {
    assert(mv_shade_begin(s,depth,61u*51u,4,8,61,51,sample,NULL));
}
static bool cancel(void) { return false; }
static void raster_tests(void) {
    struct { uint16_t pre,depth[61*51],post; } d={.pre=0xa123,.post=0xb456};
    mv_shade_surface_t s={0};
    uint8_t image[W*H],reference[W*H];
    assert(!mv_shade_begin(&s,d.depth,1,4,8,61,51,1,NULL));
    assert(!mv_shade_begin(&s,d.depth,61*51,4,8,61,51,0,NULL));
    assert(!mv_shade_begin(&s,d.depth,61*51,4,8,-1,51,1,NULL));
    assert(!mv_shade_begin(&s,d.depth,61*51,4,8,4097,51,1,NULL));
    assert(mv_shade_begin(&s,d.depth,61*51,4,8,61,51,1,NULL));
    for (int density=0; density<=16; ++density) {
        int black=0;
        for (int y=0;y<4;++y) for(int x=0;x<4;++x) black+=mv_shade_black(x,y,(uint8_t)density);
        assert(black==density);
    }
    mv_shade_vertex_t a={4,8,0},b={65,8,0},c={65,59,0},e={4,59,0};
    for(int sample=1;sample<=2;++sample) {
        memset(image,0xcc,sizeof(image)); begin(&s,d.depth,sample);
        assert(mv_shade_triangle(&s,image,pixel,a,b,c,6));
        assert(mv_shade_triangle(&s,image,pixel,a,c,e,6));
        for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
            const bool inside=x>=4 && x<65 && y>=8 && y<59;
            assert(image[y*W+x]==(inside?(uint8_t)mv_shade_black(x,y,6):0xcc));
        }
        assert(d.pre==0xa123 && d.post==0xb456);
    }
    /* Draw-order independent occlusion, including erasing rear black pixels
       through the white samples of the foreground material. */
    a=(mv_shade_vertex_t){6,10,-0.5f}; b=(mv_shade_vertex_t){62,10,-0.5f}; c=(mv_shade_vertex_t){6,57,-0.5f};
    mv_shade_vertex_t na=a,nb=b,nc=c; na.z=nb.z=nc.z=0.5f;
    for(int order=0;order<2;++order) {
        memset(image,0,sizeof(image)); begin(&s,d.depth,1);
        if(order) assert(mv_shade_triangle(&s,image,pixel,na,nb,nc,2));
        assert(mv_shade_triangle(&s,image,pixel,a,b,c,14));
        if(!order) assert(mv_shade_triangle(&s,image,pixel,na,nb,nc,2));
        if(!order) memcpy(reference,image,sizeof(image));
        else assert(!memcmp(image,reference,sizeof(image)));
    }
    for(int y=12;y<20;++y) for(int x=8;x<16;++x) assert(image[y*W+x]==mv_shade_black(x,y,2));
    /* Intersecting surfaces: nearer changes across the triangle, so painter
       sorting or using one average depth per face cannot pass this check. */
    a.z=-0.8f; b.z=0.8f; c.z=-0.8f; na.z=nb.z=nc.z=0;
    memset(image,0,sizeof(image)); begin(&s,d.depth,1);
    assert(mv_shade_triangle(&s,image,pixel,na,nb,nc,2));
    assert(mv_shade_triangle(&s,image,pixel,a,b,c,14));
    for(int x=8;x<16;++x) assert(image[12*W+x]==mv_shade_black(x,12,2));
    for(int x=44;x<52;++x) assert(image[12*W+x]==mv_shade_black(x,12,14));
    /* Reversed winding, degenerate/offscreen/nonfinite geometry and service
       cancellation must never touch memory outside the viewport. */
    memcpy(reference,image,sizeof(image));
    writes=0;
    assert(mv_shade_triangle(&s,image,pixel,a,a,a,9));
    assert(mv_shade_triangle(&s,image,pixel,(mv_shade_vertex_t){-1e10f,0,0},(mv_shade_vertex_t){-1e10f,1,0},(mv_shade_vertex_t){-1e10f,2,0},9));
    assert(mv_shade_triangle(&s,image,pixel,(mv_shade_vertex_t){NAN,0,0},b,c,9));
    assert(writes==0 && !memcmp(reference,image,sizeof(image)));
    memset(image,0,sizeof(image)); begin(&s,d.depth,1);
    assert(mv_shade_triangle(&s,image,pixel,na,nb,nc,7));
    memcpy(reference,image,sizeof(image)); memset(image,0,sizeof(image)); begin(&s,d.depth,1);
    assert(mv_shade_triangle(&s,image,pixel,na,nc,nb,7));
    assert(!memcmp(reference,image,sizeof(image)));
    s.service=cancel;
    assert(!mv_shade_triangle(&s,image,pixel,na,nb,nc,7));
    assert(d.pre==0xa123 && d.post==0xb456);
    /* An enormous clipped triangle crosses every viewport boundary. */
    begin(&s,d.depth,1); writes=0;
    assert(mv_shade_triangle(&s,image,pixel,(mv_shade_vertex_t){-400,-400,0},(mv_shade_vertex_t){800,-400,0},(mv_shade_vertex_t){200,1000,0},8));
    assert(writes>0 && d.pre==0xa123 && d.post==0xb456);
    puts("raster: fill, dither, occlusion, interpolated depth, winding, bounds, cancellation PASS");
}
static void lighting_tests(void) {
    const mv_shade_vertex_t o={0,0,0},x={1,0,0},y={0,1,0},tilt={0,1,1},side={1,0,1};
    const unsigned front=mv_shade_density(o,x,y),dark=mv_shade_density(o,x,tilt),light=mv_shade_density(o,y,side);
    assert(front>=2 && front<=14 && dark>=2 && dark<=14 && light>=2 && light<=14);
    assert(front!=dark && front!=light && light!=dark);
    assert(front==mv_shade_density(o,y,x));
    assert(dark==mv_shade_density(o,tilt,x));
    assert(light==mv_shade_density(o,side,y));
    assert(mv_shade_density(o,o,o)==9);
    puts("lighting: distinct face tones and reversed winding PASS");
}
static unsigned allocations,frees;
static bool fail_alloc;
static void *allocate(size_t n) { ++allocations; assert(n==MV_DEPTH_COUNT*sizeof(uint16_t)); return fail_alloc?NULL:malloc(n); }
static void release(void *p) { ++frees; free(p); }
static mv_contacts_t contact(int x,int y) { return (mv_contacts_t){.count=1,.id={7},.x={x},.y={y}}; }
static bool tap(int x,int y,uint32_t ms) {
    mv_contacts_t up={0},down=contact(x,y);
    assert(!mv_handle_touch(&up,&down,ms));
    return mv_handle_touch(&down,&up,ms+50);
}
static void gesture_tests(void) {
    app_services.psram_alloc=allocate; app_services.psram_free=release;
    g_use_psram=true;
    mv_reset_view(); g_view.zoom=2.0f; g_view.pan_x=37; g_view.yaw=0.91f;
    const mv_view_t saved=g_view;
    const int x=MV_SHADE_LEFT+30,y=MV_SHADE_TOP+20;
    assert(tap(x,y,1000) && g_shaded && g_depth && g_need_refine);
    assert(!memcmp(&saved,&g_view,sizeof(saved)) && g_last_tap_ms==0);
    assert(tap(x,y,1100) && !g_shaded && !g_depth);
    assert(!memcmp(&saved,&g_view,sizeof(saved)) && allocations==1 && frees==1);
    mv_contacts_t up={0},down=contact(x,y),out=contact(x,200);
    assert(!mv_handle_touch(&up,&down,2000));
    assert(!mv_handle_touch(&down,&out,2020));
    assert(!mv_handle_touch(&out,&down,2040));
    assert(!mv_handle_touch(&down,&up,2050));
    assert(!g_shaded && !memcmp(&saved,&g_view,sizeof(saved)));
    mv_contacts_t two=down; two.count=2; two.id[1]=8; two.x[1]=20; two.y[1]=200;
    assert(!mv_handle_touch(&up,&down,3000));
    assert(!mv_handle_touch(&down,&two,3020));
    assert(!mv_handle_touch(&two,&down,3040));
    assert(!mv_handle_touch(&down,&up,3050));
    assert(!g_shaded && !memcmp(&saved,&g_view,sizeof(saved)));
    assert(!mv_handle_touch(&up,&down,4000));
    assert(!mv_handle_touch(&down,&up,5000));
    assert(!g_shaded);
    mv_contacts_t other=down; other.id[0]=9;
    assert(!mv_handle_touch(&up,&down,6000));
    assert(!mv_handle_touch(&down,&other,6020));
    assert(!mv_handle_touch(&other,&up,6050));
    assert(!g_shaded);
    assert(!mv_shade_button_hit(MV_SHADE_LEFT-1,y));
    assert(!mv_shade_button_hit(MV_SHADE_RIGHT,y));
    assert(!mv_shade_button_hit(x,MV_SHADE_BOTTOM));
    assert(mv_shade_button_hit(MV_SHADE_LEFT,MV_SHADE_TOP));
    /* Allocation failure is visible and recoverable, not a blank screen. */
    fail_alloc=true; assert(tap(x,y,7000));
    assert(!g_shaded && !g_depth && strstr(g_status,"PSRAM"));
    unsigned before=allocations; g_use_psram=false;
    assert(tap(x,y,8000) && allocations==before && !g_shaded);
    g_use_psram=true; fail_alloc=false;
    assert(tap(x,y,9000) && g_shaded && !g_status[0]);
    /* Canvas double-tap still resets the camera without changing material. */
    assert(!tap(100,400,10000)); assert(tap(100,400,10100));
    assert(g_shaded && g_view.zoom==1.0f && g_view.pan_x==0.0f);
    mv_shade_release(); before=frees; mv_shade_release(); assert(frees==before);
    puts("gestures: toggle, unchanged view, tap capture/cancel, reset, PSRAM failure/cleanup PASS");
}
static bool logical_black(const uint8_t *p,int x,int y) { return (p[(539-x)*120+(y>>3)]&(0x80u>>(y&7)))!=0; }
static void button_tests(void) {
    uint8_t buffer[64800]={0};
    for(int enabled=0;enabled<2;++enabled) {
        g_shaded=enabled; memset(buffer,0,sizeof(buffer)); mv_shade_button_draw(buffer);
        const char *label=enabled?"SHADE ON":"SHADE OFF";
        const int tw=(int)strlen(label)*12-2;
        const int tx=MV_SHADE_LEFT+(MV_SHADE_RIGHT-MV_SHADE_LEFT-tw)/2;
        const int ty=MV_SHADE_TOP+(MV_SHADE_BOTTOM-MV_SHADE_TOP-14)/2;
        for(int yy=0;yy<7;++yy) for(int xx=0;xx<5;++xx) {
            bool ink=(mv_glyph('S')[xx]&(1u<<yy))!=0;
            assert(logical_black(buffer,tx+xx*2,ty+yy*2)==(enabled?!ink:ink));
        }
        assert(logical_black(buffer,MV_SHADE_LEFT+10,MV_SHADE_TOP));
        assert(!logical_black(buffer,MV_SHADE_LEFT-1,ty));
    }
    g_shaded=false;
    puts("button: top-right bounds, centered text, inverted selected state PASS");
}
static uint32_t clock_ms;
static t5_app_input_t next_input;
static uint32_t clock_now(void) { return clock_ms; }
static bool poll_input(t5_app_input_t *p,uint32_t wait) { assert(wait==1); *p=next_input; return true; }
static void service_tests(void) {
    app_services.millis=clock_now; app_services.poll=poll_input;
    g_render_service_ms=0; clock_ms=16; next_input.buttons=T5_APP_BUTTON_RIGHT;
    assert(!mv_render_service() && g_render_input_pending && g_render_input.buttons==T5_APP_BUTTON_RIGHT);
    g_render_input_pending=false; clock_ms=32; next_input.buttons=0; next_input.exit_requested=true;
    assert(!mv_render_service() && g_render_input_pending && g_render_input.exit_requested);
    puts("render service: navigation and exit retained on aborted frame PASS");
}
int main(void) { raster_tests(); lighting_tests(); gesture_tests(); button_tests(); service_tests(); return 0; }
'''


def translation_unit():
    constants='\n'.join(re.findall(r'^#define MV_[^\n]+',APP,re.M))
    types=APP[APP.index('typedef struct { float x, y, z; }'):APP.index('static const t5_app_api_v1')]
    globals_=APP[APP.index('static bool g_use_psram;'):APP.index('static int mv_iabs')]
    names=['mv_iabs','mv_clampf','mv_wrap_angle','mv_alloc','mv_free','mv_pixel','mv_glyph',
           'mv_text_ink','mv_shade_button_hit','mv_shade_button_draw','mv_shade_release',
           'mv_shade_toggle','mv_reset_view','mv_contact_index','mv_approx_distance',
           'mv_handle_touch','mv_render_service']
    return HARNESS+'\n'+constants+'\n'+types+globals_+'\n'.join(function(n) for n in names)+TESTS


class ShadingTests(unittest.TestCase):
    def test_rasterizer_and_app_helpers(self):
        cc=shutil.which(os.environ.get('CC','cc'))
        self.assertIsNotNone(cc,'A host C compiler is required')
        with tempfile.TemporaryDirectory(prefix='model-viewer-shading-') as tmp:
            source=Path(tmp)/'test.c'; binary=Path(tmp)/'test'
            source.write_text(translation_unit(),encoding='utf-8')
            cmd=[cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function',
                 '-Wno-unused-variable','-I',str(ROOT/'Apps'),str(source),'-o',str(binary)]
            if os.environ.get('MV_SANITIZE')=='1':
                cmd[2:2]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            subprocess.run(cmd,check=True)
            subprocess.run([str(binary)],check=True)

    def test_integration_keeps_full_geometry_and_display_boundary(self):
        self.assertIn('g_shaded ? 1u : mv_render_step(interactive)',APP)
        self.assertIn('interactive?2:1,mv_render_service',APP)
        self.assertIn('g_need_refine=!mv_render(false)',APP)
        self.assertIn('if (g_render_input_pending)',APP)
        self.assertIn('mv_shade_release();\n    mv_touch_end();',APP)
        shader=(ROOT/'Apps/model_viewer_shading.h').read_text()
        self.assertNotIn('T5VideoApi',shader)
        self.assertNotIn('HalDisplay',shader)
        self.assertNotIn('t5_app_get_api',shader)


if __name__=='__main__':
    unittest.main()
