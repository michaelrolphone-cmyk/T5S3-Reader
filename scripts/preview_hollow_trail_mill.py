#!/usr/bin/env python3
"""Render the production mill/desk states and lossless before/after comparisons."""
import argparse,hashlib,json,pathlib,subprocess,tempfile
from PIL import Image,ImageDraw
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1])
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--compare-before',type=pathlib.Path)
a=p.parse_args();r=a.source_root.resolve();out=a.output;out.mkdir(parents=True,exist_ok=True)
src=r'''
#include <stdio.h>
#include <stdlib.h>
#include "@ENGINE@"
#include "@CUTSCENE@"
int main(int argc,char **argv){
 if(argc!=3)return 2;uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8);
 if(!mem||!bits)return 3;ht_bind(mem);ht_bind_native(mem);ht_camera_mode=HT_CAMERA_NATIVE;
 memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);
 ht.traversal.forest_log_phase=ht.traversal.bridge_open=32;
 int x=!strcmp(argv[1],"drawn")?1197:!strcmp(argv[1],"arrival")?1071:1111;
 ht.x=x*256;ht.y=ht_land_height(0,x<1080?2:3,x)*256;ht.grounded=true;
 if(!strcmp(argv[1],"drawn")){
#ifdef HT_MILL_DESK_START
  /* Produce the light state with real grip + movement, not a moved fixture. */
  ht.x=(HT_MILL_DESK_START+17)*256;ht.y=ht_land_height(0,3,ht.x/256)*256;
  if(!ht_traversal_interact())return 7;
  unsigned steps=0;while(ht.traversal.crate_x/256<1180 && steps++<180)ht_step_controls(1,0,false,false);
  if(steps>=180 || ht_inspect()!=1)return 8;
#else
  if(ht_inspect()!=1)return 9;
#endif
 }
 ht.camera=944*256;ht.camera_y=20*256;ht.intimacy=256;ht.vista=0;
 ht.sway_phase=ht.rotation_phase=ht.drop_zoom=0;ht.ticks=8;ht.story_x=ht.x/256;
 if(!strcmp(argv[1],"arrival")){ht_cutscene_begin(HT_CUTSCENE_MILL);ht_cutscene.tick=170;ht_cutscene_render(&ht_cutscene);}
 else{ht_render_scene();ht_narration(&ht);ht_traversal_prompt(&ht);ht_evidence_prompt(&ht);}
 char path[1024];snprintf(path,sizeof(path),"%s.pgm",argv[2]);FILE *f=fopen(path,"wb");if(!f)return 4;
 fprintf(f,"P5\n960 540\n255\n");for(unsigned i=0;i<HT_NATIVE_PIXELS;++i)fputc(255-ht_scene[i],f);fclose(f);
 ht_pack_mono(bits,120);snprintf(path,sizeof(path),"%s.pbm",argv[2]);f=fopen(path,"wb");if(!f)return 4;
 fprintf(f,"P4\n960 540\n");fwrite(bits,1,HT_NATIVE_PIXELS/8,f);fclose(f);free(bits);free(mem);return 0;
}
'''.replace('@ENGINE@',str(r/'Apps/hollow_trail_engine.inc')).replace('@CUTSCENE@',str(r/'Apps/hollow_trail_cutscene.inc'))
with tempfile.TemporaryDirectory(prefix='mill-preview-') as t:
 c=pathlib.Path(t)/'capture.c';c.write_text(src);exe=pathlib.Path(t)/'capture'
 subprocess.run(['cc','-std=c11','-O2','-Wno-unused-function','-I'+str(r/'lib/NativeApps/include'),str(c),'-o',str(exe)],check=True)
 captures=[]
 for name in ['shadow','drawn','arrival']:
  stem=pathlib.Path(t)/name;subprocess.run([str(exe),name,str(stem)],check=True);hashes={}
  for extension,suffix in [('.pgm',''),('.pbm','-mono')]:
   img=Image.open(stem.with_suffix(extension));img.save(out/(name+suffix+'.png'));hashes['mono' if suffix else 'gray']=hashlib.sha256(img.tobytes()).hexdigest()
  captures.append({'view':name,'raster':[960,540],'hashes':hashes})
 files=sorted((r/'Apps').glob('hollow_trail*'))
 meta={'version':json.loads((r/'Apps/hollow_trail.json').read_text())['version'],'source_files':{str(f.relative_to(r)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files if f.is_file()},'captures':captures,'fixture':{'camera':[944,20],'ticks':8,'arrival_tick':170,'shadow_actor_x':1111,'drawn_before_actor_x':1197,'drawn_after':'actual A grip + Right until desk reaches1180, then inspect; no body teleport'}}
 (out/'capture-metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
if a.compare_before:
 before=a.compare_before;before_meta=json.loads((before/'capture-metadata.json').read_text());pairs=[]
 for name in ['shadow','drawn','arrival']:
  canvas=Image.new('RGB',(1920,1134),'#e9e6df');d=ImageDraw.Draw(canvas)
  d.text((12,8),'BEFORE '+before_meta['version']+' | '+name,fill='#252525');d.text((972,8),'AFTER '+meta['version']+' | '+name,fill='#252525');counts={}
  for row,suffix in enumerate(['','-mono']):
   images=[Image.open(folder/(name+suffix+'.png')).convert('RGB') for folder in (before,out)]
   for col,image in enumerate(images):
    canvas.paste(image,(960*col,26+560*row));assert canvas.crop((960*col,26+560*row,960*col+960,566+560*row)).tobytes()==image.tobytes()
   left,right=(image.tobytes() for image in images)
   counts['mono' if suffix else 'gray']=sum(left[i:i+3]!=right[i:i+3] for i in range(0,len(left),3))
  d.text((12,570),'Production packed monochrome; full-size original pixels',fill='#252525');canvas.save(out/(name+'-before-after.png'));pairs.append({'view':name,'changed_pixels':counts})
 (out/'comparison-metadata.json').write_text(json.dumps({'before':before_meta,'after':meta,'comparisons':pairs,'canvas_check':'Original raster regions verified byte-for-byte.'},indent=2)+'\n')
print(out)
