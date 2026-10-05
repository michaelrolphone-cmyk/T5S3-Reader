#!/usr/bin/env python3
"""Capture the actual Chapter XI app/input/reader/raster path and old loop exit."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile
from PIL import Image, ImageDraw
from hollow_trail_capture_source import source_identity, source_caption

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
parser.add_argument('--source-ref')
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--compare-before', type=pathlib.Path)
args = parser.parse_args()
root, out = args.source_root.resolve(), args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "@APP@"
static uint32_t now,buttons;
static bool poll_input(t5_app_input_t *out,uint32_t wait) {
 now+=wait;memset(out,0,sizeof(*out));out->buttons=buttons;return true;
}
static uint32_t clock_ms(void){return now;}
static const t5_app_api_v1 fake_app={.abi_version=1,.struct_size=sizeof(fake_app),.poll=poll_input,.millis=clock_ms};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &fake_app;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
static void input(uint32_t b){buttons=b;ht_input(1);}
static void press(uint32_t b){input(0);input(b);}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static const char *folder;
static uint8_t *bits;
static void capture(const char *name) {
 char path[1024];
 if(reading){ht_journal_render();ht_read_submitted_revision=scene_revision;}
 else {ht_render_scene();ht_narration(&ht);ht_pack_mono(bits,120);}
 snprintf(path,sizeof(path),"%s/%s.pgm",folder,name);FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"P5\n960 540\n255\n");
 for(int y=0;y<540;++y)for(int x=0;x<960;++x)
  fputc(255-ht_scene[ht_native_active?y*960+x:(y/2)*480+x/2],f);
 fclose(f);
 snprintf(path,sizeof(path),"%s/%s.pbm",folder,name);f=fopen(path,"wb");assert(f);
 fprintf(f,"P4\n960 540\n");fwrite(bits,1,HT_NATIVE_PIXELS/8,f);fclose(f);
 printf("%s\n",name);
}
#ifdef HT_DOOR_STOP_X
static void walk(int target){input(T5_APP_BUTTON_RIGHT);unsigned n=0;
 while(ht.x/256<target && n++<1600)ticks(1);assert(n<1600);input(0);ticks(8);}
static void until_stage(unsigned stage){unsigned n=0;
 while(ht.door_stage!=stage && n++<1600)ticks(1);assert(n<1600);input(0);}
#endif
int main(int argc,char **argv) {
 assert(argc==2);folder=argv[1];
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY);bits=malloc(HT_NATIVE_PIXELS/8);assert(memory && bits);
 ht_bind(memory);ht_bind_native(memory);ht_camera_mode=HT_CAMERA_NATIVE;app=&fake_app;ht_reader_bitmap=bits;
 memset(&ht,0,sizeof(ht));ht.level=HT_LEVELS-1;ht_select_level(ht.level);ht_spawn(true);
 ht.puzzle.solved=true;ht.puzzle.opening=48;ht.x=HT_GOAL*256;ht.y=ht_land[9].top*256;
 ht.grounded=true;ht.verdict=ht.last_verdict=1;ht.verdict_read=true;ht.endings=1;ht.evidence=(1u<<30)-1;
 input(0);ticks(1);ht_select_level(ht.level);capture("exit");
#ifdef HT_DOOR_STOP_X
 assert(ht.door_stage==HT_DOOR_YARD);input(0);walk(HT_DOOR_BARREL);
 press(T5_APP_BUTTON_CONFIRM);input(0);ticks(240);capture("yard-rest");
 until_stage(HT_DOOR_STREET);walk(800);capture("unwired-street");walk(HT_DOOR_STOP_X);
 press(T5_APP_BUTTON_CONFIRM);input(0);ticks(140);capture("shoes");until_stage(HT_DOOR_NOTEBOOK);
 press(T5_APP_BUTTON_CONFIRM);assert(reading);capture("notebook-first");
 unsigned n=0;while(ht_journal_next<ht_journal_length && n++<63){press(T5_APP_BUTTON_RIGHT);ht_journal_render();ht_read_submitted_revision=scene_revision;}
 assert(n<63);capture("notebook-last");press(T5_APP_BUTTON_CONFIRM);assert(!reading);input(0);
 ticks(210);capture("grief");until_stage(HT_DOOR_STRAIGHTEN);ticks(160);capture("upright-shoes");
 until_stage(HT_DOOR_KNOCK);ticks(52);capture("ordinary-knock");until_stage(HT_DOOR_WAIT);capture("waiting");
 ht_game saved=ht;input(T5_APP_BUTTON_RIGHT|T5_APP_BUTTON_UP);ticks(10000);assert(!memcmp(&ht,&saved,sizeof(ht)));capture("waiting-later");
#endif
 free(bits);free(memory);return 0;
}
'''.replace('@APP@', str(root / 'Apps/hollow_trail.c'))
with tempfile.TemporaryDirectory(prefix='final-door-preview-') as temporary:
    temporary = pathlib.Path(temporary)
    code, exe = temporary / 'capture.c', temporary / 'capture'
    code.write_text(source)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wno-unused-function', '-I'+str(root/'lib/NativeApps/include'),
                    '-I'+str(root/'sdk/driver'), str(code), '-o', str(exe)], check=True)
    names = subprocess.check_output([str(exe), str(temporary)], text=True).splitlines()
    captures = []
    for name in names:
        hashes = {}
        for extension, suffix in [('.pgm', ''), ('.pbm', '-mono')]:
            image = Image.open(temporary/(name+extension))
            image.save(out/(name+suffix+'.png'))
            hashes['mono' if suffix else 'gray'] = hashlib.sha256(image.tobytes()).hexdigest()
        captures.append({'view': name, 'raster': [960, 540], 'hashes': hashes})
metadata = {'version': json.loads((root/'Apps/hollow_trail.json').read_text())['version'],
            'source_files': {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest()
                             for f in sorted((root/'Apps').glob('hollow_trail*')) if f.is_file()},
            'captures': captures,
            'fixture': 'Identical reached tower exit, black choice, read testimony and all evidence. Then production device A/Right inputs, fixed-step holds and submitted compact notebook pages.',
            'baseline': 'The prior source returns to the forest after that tower exit. No Chapter XI scene existed.',
            'notebook': 'Reader service absent: actual built-in compact fallback, with packed 960x540 production bitmap.'}
metadata.update(source_identity(root, metadata['source_files'], args.source_ref))
(out/'capture-metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
if (out/'waiting.png').exists():
    for suffix in ['', '-mono']:
        assert (out/('waiting'+suffix+'.png')).read_bytes() == (out/('waiting-later'+suffix+'.png')).read_bytes()
    views = ['exit', 'yard-rest', 'unwired-street', 'shoes', 'notebook-last', 'grief', 'upright-shoes', 'waiting']
    sheet = Image.new('RGB', (1920, 4*578), '#e9e6df')
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(views):
        x, y = (index%2)*960, (index//2)*578
        draw.text((x+12, y+10), name.upper(), fill='#252525')
        sheet.paste(Image.open(out/(name+'.png')).convert('RGB'), (x, y+30))
    sheet.save(out/'scene-contact-sheet.png')
if args.compare_before:
    before = args.compare_before.resolve()
    comparisons = []
    for suffix in ['', '-mono']:
        left = Image.open(before/('exit'+suffix+'.png')).convert('RGB')
        right = Image.open(out/('waiting'+suffix+'.png')).convert('RGB')
        canvas = Image.new('RGB', (1920, 578), '#e9e6df');draw = ImageDraw.Draw(canvas)
        draw.text((12,10), 'BEFORE '+source_caption(json.loads((before/'capture-metadata.json').read_text()))+' | TOWER EXIT', fill='#252525')
        draw.text((972,10), 'AFTER '+source_caption(metadata)+' | UNANSWERED DOOR', fill='#252525')
        canvas.paste(left, (0,30));canvas.paste(right, (960,30))
        assert canvas.crop((0,30,960,570)).tobytes() == left.tobytes()
        assert canvas.crop((960,30,1920,570)).tobytes() == right.tobytes()
        canvas.save(out/('ending-before-after'+suffix+'.png'))
        comparisons.append({'format': 'mono' if suffix else 'gray', 'left': 'before exit', 'right': 'after waiting', 'lossless_crop_check': True})
    (out/'comparison-metadata.json').write_text(json.dumps({'before': json.loads((before/'capture-metadata.json').read_text()), 'after': metadata, 'comparisons': comparisons}, indent=2)+'\n')
print(out)
