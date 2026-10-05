#!/usr/bin/env python3
"""Capture real mill interaction, memory and journal on either source snapshot.

Run this same script for baseline and changed source. Every inspection follows
actual device Confirm/Right input that draws the desk into its window light.
The baseline legitimately remains in its old journal during the new memory.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile
from PIL import Image, ImageDraw

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
p.add_argument('--source-ref')
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--compare-before', type=pathlib.Path)
a = p.parse_args()
r = a.source_root.resolve()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
ref = a.source_ref or 'HEAD'
if a.source_ref:
    subprocess.run(['git', '-C', str(r), 'diff', '--exit-code', ref, '--', 'Apps'], check=True)
commit = subprocess.check_output(['git', '-C', str(r), 'rev-parse', ref], text=True).strip()
shots = [('interior', 0), ('register', 90), ('desk-writing', 250), ('wrist-hair', 355),
         ('second-loaf', 530), ('packed-loaf', 675), ('doorway', 815),
         ('trembling-page', 930), ('covered-names', 1070), ('uncovered-names', 1240),
         ('keeper-line', 1360), ('read-register', 1440)]
source = r'''
#include <stdio.h>
#include <stdlib.h>
#include "@APP@"
static uint32_t capture_now,capture_buttons;
static bool capture_poll(t5_app_input_t *out,uint32_t wait) {
 capture_now+=wait;memset(out,0,sizeof(*out));out->buttons=capture_buttons;return true;
}
static uint32_t capture_millis(void){return capture_now;}
static const t5_app_api_v1 capture_api={.abi_version=1,.struct_size=sizeof(capture_api),.poll=capture_poll,.millis=capture_millis};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &capture_api;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
int main(int argc,char **argv){
 if(argc!=4)return 2;
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8);
 if(!mem||!bits)return 3;ht_bind(mem);ht_bind_native(mem);ht_reader_bitmap=bits;app=&capture_api;
 memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);ht_camera_mode=HT_CAMERA_NATIVE;
 ht.traversal.forest_log_phase=ht.traversal.bridge_open=32;
 ht.x=(HT_MILL_DESK_START+17)*256;ht.y=ht_land_height(0,3,ht.x/256)*256;ht.grounded=true;
 ht_cutscene.active=false;ht_cutscene_seen=1;ht_input(1);
 capture_buttons=T5_APP_BUTTON_CONFIRM;ht_input(1);if(ht.traversal.mode!=HT_CRATE)return 7;
 capture_buttons=T5_APP_BUTTON_RIGHT;
 unsigned steps=0;while(ht.traversal.crate_x/256<1180 && steps++<180)ht_input(32);
 if(steps>=180 || !ht_mill_register_lit(&ht))return 8;
 capture_buttons=0;ht_input(1);
 ht.camera=944*256;ht.camera_y=20*256;ht.intimacy=256;ht.vista=0;
 ht.sway_phase=880;ht.rotation_phase=48000;ht.camera_mood=70;
 if(strcmp(argv[1],"interior")) {
  capture_buttons=T5_APP_BUTTON_CONFIRM;ht_input(1);
  capture_buttons=0;ht_input(1);
  if(!ht_evidence_found(&ht,1))return 9;
  unsigned ticks=(unsigned)atoi(argv[2]);
  for(unsigned i=0;i<ticks;++i)ht_input(32);
 }
 bool journal=reading;
 if(journal)ht_journal_render();
 else if(ht_cutscene.active)ht_cutscene_render(&ht_cutscene);
 else {ht_render_scene();ht_narration(&ht);ht_evidence_prompt(&ht);}
 char file[1024];snprintf(file,sizeof(file),"%s.pgm",argv[3]);FILE *f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P5\n960 540\n255\n");
 for(int y=0;y<540;++y)for(int x=0;x<960;++x)fputc(255-ht_scene[journal?(y/2)*480+x/2:y*960+x],f);fclose(f);
 if(!journal)ht_pack_mono(bits,120);
 snprintf(file,sizeof(file),"%s.pbm",argv[3]);f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P4\n960 540\n");fwrite(bits,1,HT_NATIVE_PIXELS/8,f);fclose(f);
 printf("%s %u %u %d %d\n",journal?"journal":ht_cutscene.active?"memory":"game",ht_cutscene.tick,ht.evidence,ht.traversal.crate_x,ht.traversal.mill_scrape_right);
 free(bits);free(mem);return 0;
}
'''.replace('@APP@', str(r/'Apps/hollow_trail.c'))
captures = []
with tempfile.TemporaryDirectory(prefix='mill-memory-') as tmp:
    tmp = pathlib.Path(tmp)
    c, exe = tmp/'capture.c', tmp/'capture'
    c.write_text(source)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wno-unused-function', '-I'+str(r/'lib/NativeApps/include'),
                    '-I'+str(r/'sdk/driver'), str(c), '-o', str(exe)], check=True)
    sheet = Image.new('RGB', (1440, 30+4*295), '#e9e6df')
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 8), 'HOLLOW TRAIL | Real input and C renderer | Mill register memory', fill='#252525')
    for i, (name, tick) in enumerate(shots):
        stem = tmp/name
        state = subprocess.check_output([str(exe), name, str(tick), str(stem)], text=True).strip()
        hashes = {}
        for ext, suffix in [('.pgm', ''), ('.pbm', '-mono')]:
            frame = Image.open(stem.with_suffix(ext))
            frame.save(out/(name+suffix+'.png'))
            hashes['mono' if suffix else 'gray'] = hashlib.sha256(frame.tobytes()).hexdigest()
        frame = Image.open(stem.with_suffix('.pgm'))
        x, y = i%3*480, 30+i//3*295
        sheet.paste(frame.resize((480, 270), Image.Resampling.LANCZOS).convert('RGB'), (x, y))
        draw.text((x+9, y+274), name+' | '+state.split()[0], fill='#252525')
        captures.append({'view':name, 'requested_tick':tick, 'observed_state':state, 'hashes':hashes})
    sheet.save(out/'contact-sheet.png')
meta = {'source_commit':commit, 'version':json.loads((r/'Apps/hollow_trail.json').read_text())['version'],
        'source_files':{str(f.relative_to(r)):hashlib.sha256(f.read_bytes()).hexdigest()
                        for f in sorted((r/'Apps').glob('hollow_trail*')) if f.is_file()},
        'fixture':'Actual device Confirm grips desk, Right draws it to X1180, neutral, Confirm inspects. Same input for both snapshots. Journal is actual compact fallback without host typography provider.',
        'raster':[960, 540], 'captures':captures}
(out/'capture-metadata.json').write_text(json.dumps(meta, indent=2)+'\n')
if a.compare_before:
    before = a.compare_before
    before_meta = json.loads((before/'capture-metadata.json').read_text())
    pairs = []
    for name, tick in shots:
        canvas = Image.new('RGB', (1920, 1134), '#e9e6df')
        draw = ImageDraw.Draw(canvas)
        draw.text((12, 8), 'BEFORE '+before_meta['source_commit'][:12]+' | '+name, fill='#252525')
        draw.text((972, 8), 'AFTER | '+name, fill='#252525')
        counts = {}
        for row, suffix in enumerate(['', '-mono']):
            images = [Image.open(folder/(name+suffix+'.png')).convert('RGB') for folder in (before, out)]
            assert all(image.size == (960, 540) for image in images)
            for col, image in enumerate(images):
                canvas.paste(image, (960*col, 26+560*row))
                assert canvas.crop((960*col, 26+560*row, 960*col+960, 566+560*row)).tobytes() == image.tobytes()
            left, right = (image.tobytes() for image in images)
            counts['mono' if suffix else 'gray'] = sum(left[i:i+3] != right[i:i+3] for i in range(0, len(left), 3))
        draw.text((12, 570), 'Production packed monochrome; original pixels checked byte-for-byte', fill='#252525')
        canvas.save(out/(name+'-before-after.png'))
        pairs.append({'view':name, 'changed_pixels':counts})
    (out/'comparison-metadata.json').write_text(json.dumps({'before':before_meta, 'after':meta, 'pairs':pairs}, indent=2)+'\n')
print(out/'contact-sheet.png')
