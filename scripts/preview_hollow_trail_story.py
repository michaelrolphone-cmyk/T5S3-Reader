#!/usr/bin/env python3
"""Render actual Hollow Trail timeline/scene pixels and physical mono packing.
Requires a host C compiler and Pillow. No generated illustration or device FPS claim.
"""
import argparse
import json
import pathlib
import subprocess
import tempfile
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('dist/story-previews'))
selection = parser.add_mutually_exclusive_group()
selection.add_argument('--rain-shelter', action='store_true', help='Render active post-fuse rain at refuge edges, zooms and handoff')
selection.add_argument('--mill-entry', action='store_true', help='Render the barred mill door, broken shutter and register along the existing route')
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
shots = [('kitchen', 140, 'Cold cup, chipped sister cup, wall/rake/bucket'),
         ('memory', 450, 'Flattened room in depth-layered grass'),
         ('warning', 730, 'The dark upstairs window behind the warning'),
         ('night', 1060, 'A night dressed in the chair; un-rinsed cup'),
         ('packing', 1250, 'Bread, knife, stockings, lamp, notebook/accounts'),
         ('latch', 1415, 'The coat thread catches before departure'),
         ('orchard', 1830, 'Continuous doorstep / wall / orchard route'),
         ('roots', 2150, 'Domestic boundaries become root-soft road'),
         ('mill', 170, 'Playable mill hollow / one-shot arrival tableau'),
         ('gate', 0, 'Forest gate opens onto rooted gullies and towers'),
         ('city', 230, 'Player-earned city arrival: held roofscape reveal'),
         ('terrace', 0, 'First ladder puddle / worn bolt / crossed arrows'),
         ('arrival', 2020, 'Departure begins revealing the actual first parcel'),
         ('arrival', 2140, 'One walking silhouette through light-to-dark transition'),
         ('arrival', 2279, 'Last intro frame already uses live gameplay geometry'),
         ('boulder', 0, 'Irregular contrasting boulder / half-effort contact cue'),
         ('boulder', 2, 'Same readable stone shape in the pumping plain'),
         ('handoff', 2280, 'Control returned on the same first-parcel raster'),
         ('schoolroom', 0, 'Playable roof-level schoolroom and weighted map'),
         ('study', 0, 'Trace the lowered-ladder sightline'),
         ('study', 2, 'Trace the route toward the signal window'),
         ('refuge', 0, 'Playable shelter beneath the riveted tank'),
         ('rain', 170, 'Earned view across the court toward three flashes'),
         ('signal', 0, 'Signal window on the actual ladder roof')]
if args.rain_shelter:
    shots = [('wet-refuge', 8, 'Active post-fuse rain; protected belly drips'),
             ('wet-close', 80, 'Close framing; character contrast retained'),
             ('wet-wide', 200, 'Wide framing; rain remains outside the shelter'),
             ('wet-left', 80, 'Player at the left shelter edge'),
             ('wet-right', 80, 'Player at the right shelter edge'),
             ('wet-start', 0, 'Active-weather tableau entry'),
             ('wet-hold', 170, 'Shelter and opposite light during the hold'),
             ('wet-end', 419, 'Last tableau tick before control returns'),
             ('wet-handoff', 420, 'Live gameplay after tableau handoff')]
elif args.mill_entry:
    shots = [('mill-start', 0, 'Grounded approach to the mill'),
             ('mill-hold', 170, 'Arrival hold: broken shutter and barred door'),
             ('mill-end', 339, 'Last tableau tick before control returns'),
             ('mill-handoff', 340, 'Live gameplay after the mill tableau'),
             ('mill-register', 0, 'Register and sloping desk beside the shutter'),
             ('mill-door', 0, 'Barred doorway set in the existing uphill wall')]
source = r'''
#include <stdio.h>
#include <stdlib.h>
#include "@ENGINE@"
#include "@CUTSCENE@"
int main(int argc,char **argv) {
 if(argc!=4)return 2;
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8);
 if(!mem||!bits)return 3;
 ht_bind(mem);ht_bind_native(mem);memset(&ht,0,sizeof(ht));ht.level=0;ht_spawn(true);
 ht_camera_mode=HT_CAMERA_NATIVE;
 bool gameplay=false;
 if(!strncmp(argv[1],"wet-",4)) {
  ht.level=1;ht_select_level(1);ht_spawn(true);
  ht.x=(HT_RAIN_TANK_X+(!strcmp(argv[1],"wet-left")?-87:!strcmp(argv[1],"wet-right")?86:0))*256;
  ht.y=ht_rain_tank_floor()*256;ht.camera=ht.x-200*256;ht.camera_y=ht.y-180*256;ht.grounded=true;
  ht.scene_evidence=2;ht.weather_amount=256;ht.weather_age=96;
  ht.intimacy=!strcmp(argv[1],"wet-close")?256:0;ht.vista=!strcmp(argv[1],"wet-wide")?256:0;
  bool tableau=!strcmp(argv[1],"wet-start") || !strcmp(argv[1],"wet-hold") ||
               !strcmp(argv[1],"wet-end") || !strcmp(argv[1],"wet-handoff");
  ht.ticks=tableau?8u:(unsigned)atoi(argv[2]);
  if(tableau) {
   ht_cutscene_begin(HT_CUTSCENE_RAIN);
   if(!strcmp(argv[1],"wet-handoff")) {
    ht_cutscene.tick=419;ht_cutscene_step(&ht_cutscene);ht_cutscene_apply_handoff(&ht_cutscene);gameplay=true;
   }
  } else gameplay=true;
 } else if(!strcmp(argv[1],"refuge") || !strcmp(argv[1],"rain") || !strcmp(argv[1],"signal")) {
  ht.level=1;ht_select_level(1);ht_spawn(true);
  ht.x=(!strcmp(argv[1],"signal")?ht_rain_window_x():HT_RAIN_TANK_X)*256;
  ht.y=ht_surface_at(&ht,!strcmp(argv[1],"signal")?7:6,ht.x/256)*256;
  ht.camera=ht.x-200*256;ht.camera_y=ht.y-180*256;ht.grounded=true;ht.ticks=0;
  if(!strcmp(argv[1],"rain"))ht_cutscene_begin(HT_CUTSCENE_RAIN);else gameplay=true;
 } else if(!strcmp(argv[1],"study")) {
  ht_schoolroom_study_render((unsigned)atoi(argv[2]));
 } else if(!strcmp(argv[1],"schoolroom")) {
  ht.level=1;ht_select_level(1);ht_spawn(true);ht.x=535*256;
  ht.y=ht_surface_at(&ht,1,535)*256;
  ht.camera=335*256;ht.camera_y=ht.y-180*256;gameplay=true;
 } else if(!strcmp(argv[1],"boulder")) {
  ht.level=(unsigned)atoi(argv[2]);ht_select_level(ht.level);ht_spawn(true);
  ht.x=ht.traversal.ball_x-21*256;ht.y=ht.traversal.ball_y+HT_BALL_RADIUS*256;
  ht.camera=ht.x-200*256;ht.camera_y=ht.y-180*256;ht.traversal.push_hint=HT_ROLL;
  gameplay=true;
 } else if(!strcmp(argv[1],"handoff")) {
  ht_cutscene_begin(HT_CUTSCENE_INTRO);ht_cutscene.tick=HT_INTRO_TICKS;
  ht_cutscene_apply_handoff(&ht_cutscene);gameplay=true;
 } else
 if(!strcmp(argv[1],"city")) {
  ht.level=1;ht_select_level(1);ht_spawn(true);ht_cutscene_begin(HT_CUTSCENE_CITY);
 } else if(!strcmp(argv[1],"gate")) {
  ht.x=3130*256;ht.y=ht_surface_at(&ht,9,3130)*256;
  ht.camera=2930*256;ht.camera_y=(ht.y/256-180)*256;
  ht.puzzle.solved=true;ht.puzzle.opening=48;gameplay=true;
 } else if(!strcmp(argv[1],"terrace")) {
  ht.level=1;ht_select_level(1);ht_spawn(true);ht.x=380*256;ht.y=220*256;
  ht.camera=180*256;ht.camera_y=40*256;gameplay=true;
 } else if(!strcmp(argv[1],"mill") || !strncmp(argv[1],"mill-",5)) {
  int x=!strcmp(argv[1],"mill-register")?1180:!strcmp(argv[1],"mill-door")?1240:1071;
  int parcel=x<1080?2:3;
  ht.x=x*256;ht.y=ht_surface_at(&ht,parcel,x)*256;ht.camera=(x-200)*256;
  ht.camera_y=(ht.y/256-180)*256;ht.traversal.forest_log_phase=32;
  if(x!=1071)gameplay=true;
  else {
   ht_cutscene_begin(HT_CUTSCENE_MILL);
   if(!strcmp(argv[1],"mill-handoff")) {
    ht_cutscene.tick=339;ht_cutscene_step(&ht_cutscene);ht_cutscene_apply_handoff(&ht_cutscene);gameplay=true;
   }
  }
 } else ht_cutscene_begin(HT_CUTSCENE_INTRO);
 ht_cutscene.tick=(uint16_t)atoi(argv[2]);
 if(gameplay){ht_render_scene();ht_narration(&ht);}else if(strcmp(argv[1],"study"))ht_cutscene_render(&ht_cutscene);
 char path[1024];snprintf(path,sizeof(path),"%s.pgm",argv[3]);FILE *f=fopen(path,"wb");
 if(!f)return 4;fprintf(f,"P5\n960 540\n255\n");
 for(unsigned i=0;i<HT_NATIVE_PIXELS;++i)fputc(255-ht_scene[i],f);fclose(f);
 ht_pack_mono(bits,120);snprintf(path,sizeof(path),"%s.pbm",argv[3]);f=fopen(path,"wb");
 if(!f)return 4;fprintf(f,"P4\n960 540\n");fwrite(bits,1,HT_NATIVE_PIXELS/8,f);fclose(f);
 free(bits);free(mem);return 0;
}
'''.replace('@ENGINE@', str(repo / 'Apps/hollow_trail_engine.inc')).replace('@CUTSCENE@', str(repo / 'Apps/hollow_trail_cutscene.inc'))
with tempfile.TemporaryDirectory(prefix='hollow-preview-') as tmp:
    src = pathlib.Path(tmp) / 'preview.c'
    binary = pathlib.Path(tmp) / 'preview'
    src.write_text(source)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wno-unused-function', '-I' + str(repo / 'lib/NativeApps/include'), str(src), '-o', str(binary)], check=True)
    sheet = Image.new('RGB', (1440, 30 + ((len(shots)+2)//3)*295), '#e9e6df')
    draw = ImageDraw.Draw(sheet)
    version = json.loads((repo / 'Apps/hollow_trail.json').read_text())['version']
    draw.text((16, 8), f'HOLLOW TRAIL {version} | Actual host-rendered scenes | Native raster reduced for contact sheet; no device qualification', fill='#252525')
    for i, (name, tick, label) in enumerate(shots):
        stem = pathlib.Path(tmp) / name
        subprocess.run([str(binary), name, str(tick), str(stem)], check=True)
        frame = Image.open(stem.with_suffix('.pgm'))
        frame.save(out / (name + str(tick) + '.png'))
        Image.open(stem.with_suffix('.pbm')).save(out / (name + str(tick) + '-mono.png'))
        x, y = i % 3 * 480, 30 + i // 3 * 295
        sheet.paste(frame.resize((480, 270), Image.Resampling.LANCZOS).convert('RGB'), (x, y))
        draw.text((x + 9, y + 274), label, fill='#252525')
    sheet.save(out / 'story-progress.png')
print(out / 'story-progress.png')
