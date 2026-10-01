#!/usr/bin/env python3
"""Render actual Hollow Trail timeline/scene pixels and physical mono packing.
Requires a host C compiler and Pillow. No generated illustration or device FPS claim.
"""
import argparse
import pathlib
import subprocess
import tempfile
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('dist/story-previews'))
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
         ('terrace', 0, 'First ladder puddle / worn bolt / crossed arrows')]
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
 if(!strcmp(argv[1],"city")) {
  ht.level=1;ht_select_level(1);ht_spawn(true);ht_cutscene_begin(HT_CUTSCENE_CITY);
 } else if(!strcmp(argv[1],"gate")) {
  ht.x=3130*256;ht.y=ht_surface_at(&ht,9,3130)*256;
  ht.camera=2930*256;ht.camera_y=(ht.y/256-180)*256;
  ht.puzzle.solved=true;ht.puzzle.opening=48;gameplay=true;
 } else if(!strcmp(argv[1],"terrace")) {
  ht.level=1;ht_select_level(1);ht_spawn(true);ht.x=380*256;ht.y=220*256;
  ht.camera=180*256;ht.camera_y=40*256;gameplay=true;
 } else if(!strcmp(argv[1],"mill")) {
  ht.x=1071*256;ht.y=ht_surface_at(&ht,2,1071)*256;ht.camera=871*256;
  ht.camera_y=(ht.y/256-180)*256;ht.traversal.forest_log_phase=32;
  ht_cutscene_begin(HT_CUTSCENE_MILL);
 } else ht_cutscene_begin(HT_CUTSCENE_INTRO);
 ht_cutscene.tick=(uint16_t)atoi(argv[2]);
 if(gameplay)ht_render_scene();else ht_cutscene_render(&ht_cutscene);
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
    sheet = Image.new('RGB', (1440, 1210), '#e9e6df')
    draw = ImageDraw.Draw(sheet)
    draw.text((16, 8), 'HOLLOW TRAIL 1.1.36 | Actual host-rendered scenes | Native raster reduced for contact sheet; no device qualification', fill='#252525')
    for i, (name, tick, label) in enumerate(shots):
        stem = pathlib.Path(tmp) / name
        subprocess.run([str(binary), name, str(tick), str(stem)], check=True)
        frame = Image.open(stem.with_suffix('.pgm'))
        frame.save(out / (name + '.png'))
        Image.open(stem.with_suffix('.pbm')).save(out / (name + '-mono.png'))
        x, y = i % 3 * 480, 30 + i // 3 * 295
        sheet.paste(frame.resize((480, 270), Image.Resampling.LANCZOS).convert('RGB'), (x, y))
        draw.text((x + 9, y + 274), label, fill='#252525')
    sheet.save(out / 'story-progress.png')
print(out / 'story-progress.png')
