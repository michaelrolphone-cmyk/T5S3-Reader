#!/usr/bin/env python3
"""Capture the real first-house route, food/drink interaction and both raster modes."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile
from PIL import Image, ImageDraw
from hollow_trail_capture_source import source_identity, source_caption

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
shots = [('outside',0),('water',0),('drink',144),('wait',48),('slow-tap',216),('drink-again',80),('food',0),('biscuit',0),('bite',48),('recovery',192),('note',0),('closing',64),('returned',0)]
source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "@APP@"
#include "@ROUTE@"
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
 memset(&ht,0,sizeof(ht));ht.level=6;ht_select_level(6);ht_spawn(true);ht_camera_mode=HT_CAMERA_NATIVE;

 bool outside=!strcmp(argv[1],"outside");int target=373;
 ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;ht_input(1);
 bool jumped=false;
 for(int i=0;i<2000 && (abs(ht.x/256-target)>2 || !ht.grounded);++i){
  capture_buttons=abs(ht.x/256-target)<=2?0:ht.x/256<target?T5_APP_BUTTON_RIGHT:T5_APP_BUTTON_LEFT;
  if(!jumped && ht.x/256>=294 && ht.grounded){capture_buttons|=T5_APP_BUTTON_UP;jumped=true;}
  ht_input(32);
 }
 capture_buttons=0;ht_input(1);assert(ht.level==6 && !ht.deaths && ht.grounded && abs(ht.x/256-target)<=2);
 unsigned ticks=(unsigned)atoi(argv[2]);
#ifdef HT_FOOD_STUDY
 if(!outside){
  capture_buttons=T5_APP_BUTTON_CONFIRM;ht_input(1);capture_buttons=0;ht_input(1);assert(ht_food.active);
  int target_stage=!strcmp(argv[1],"water")?0:!strcmp(argv[1],"drink")?1:!strcmp(argv[1],"wait")?2:!strcmp(argv[1],"slow-tap")?3:!strcmp(argv[1],"drink-again")?4:!strcmp(argv[1],"food")?7:!strcmp(argv[1],"biscuit")?9:!strcmp(argv[1],"bite") || !strcmp(argv[1],"recovery")?10:!strcmp(argv[1],"note")?12:!strcmp(argv[1],"closing")?13:14;
  for(int i=0;i<4000 && ht_food.active && ht_food.stage<target_stage;++i){
   if(!ht_food_limit(ht_food.stage)){capture_buttons=T5_APP_BUTTON_CONFIRM;ht_input(1);capture_buttons=0;ht_input(1);}
   else ht_input(32);
  }
  for(unsigned i=0;i<ticks;++i)ht_input(32);
 }
#endif
 bool journal=false;
#ifdef HT_FOOD_STUDY
 if(ht_food.active)ht_food_study_render(&ht,&ht_food);
 else
#endif
 {ht.camera=(373+35-240)*256;ht.camera_y=(ht_land_height(6,0,373)-172)*256;ht.intimacy=512;ht.vista=ht.drop_zoom=0;ht.sway_phase=ht.rotation_phase=0;ht_frame_camera(&ht,HT_CAMERA_BASELINE);ht_render_scene();}
 char file[1024];snprintf(file,sizeof(file),"%s.pgm",argv[3]);FILE *f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P5\n960 540\n255\n");
 for(int y=0;y<540;++y)for(int x=0;x<960;++x)fputc(255-ht_scene[journal?(y/2)*480+x/2:y*960+x],f);fclose(f);
 if(!journal)ht_pack_mono(bits,120);
 snprintf(file,sizeof(file),"%s.pbm",argv[3]);f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P4\n960 540\n");fwrite(bits,1,HT_NATIVE_PIXELS/8,f);fclose(f);
 const char *mode=journal?"journal":"game";
#ifdef HT_FOOD_STUDY
 if(ht_food.active)mode="food";
#endif
 int stage=-1,observed_tick=0,ambient=0;
#ifdef HT_FOOD_STUDY
 if(ht_food.active){stage=ht_food.stage;observed_tick=ht_food.tick;ambient=ht_food.ambient;}
#endif
 printf("%s requested_tick=%u evidence=%u x=%d y=%d stage=%d tick=%d ambient=%d\n",mode,ticks,ht.evidence,ht.x,ht.y,stage,observed_tick,ambient);
 free(bits);free(mem);return 0;
}
'''.replace('@APP@', str(r/'Apps/hollow_trail.c')).replace('@ROUTE@', str(r/'test/native_apps/hollow_trail_route_walk.inc'))
captures = []
with tempfile.TemporaryDirectory(prefix='food-preview-') as tmp:
    tmp = pathlib.Path(tmp)
    c, exe = tmp/'capture.c', tmp/'capture'
    c.write_text(source)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wno-unused-function', '-I'+str(r/'lib/NativeApps/include'),
                    '-I'+str(r/'sdk/driver'), str(c), '-o', str(exe)], check=True)
    sheet = Image.new('RGB', (1440, 30+5*295), '#e9e6df')
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 8), 'HOLLOW TRAIL | Real input and C renderer | First-house water and food', fill='#252525')
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
        'fixture': 'The player walks from the real glasshouse spawn and jumps past the original crate. Normal mapped input earns first drink, mandatory wait, deliberate second drink, cupboard opening, one biscuit, eating/recovery, reading the inner-door note and gentle closure. Before remains in ordinary play at the same position. Terrain, crate, mirrors and evidence identities are unchanged.',
    'raster':[960, 540], 'captures':captures}
meta.update(source_identity(r, meta['source_files'], a.source_ref))
(out/'capture-metadata.json').write_text(json.dumps(meta, indent=2)+'\n')
if a.compare_before:
    before = a.compare_before
    before_meta = json.loads((before/'capture-metadata.json').read_text())
    pairs = []
    for name, tick in shots:
        canvas = Image.new('RGB', (1920, 1134), '#e9e6df')
        draw = ImageDraw.Draw(canvas)
        draw.text((12, 8), 'BEFORE '+source_caption(before_meta)+' | '+name, fill='#252525')
        draw.text((972, 8), 'AFTER '+source_caption(meta)+' | '+name, fill='#252525')
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
