#!/usr/bin/env python3
"""Capture actual rail inputs, lower bracing, wool and retained rope handoff."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile
from PIL import Image, ImageDraw
from hollow_trail_capture_source import source_identity, source_caption
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1])
p.add_argument('--source-ref')
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--compare-before',type=pathlib.Path)
a=p.parse_args()
r,out=a.source_root.resolve(),a.output.resolve()
out.mkdir(parents=True,exist_ok=True)
shots=[('approach',0),('lowering',48),('bracing',136),('wool',194),('next-footing',210),('planted-toe',226),('weight-follows',254),('crossed',330),('ascending',394),('rope-approach',442)]
source=r'''
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
static void input(uint32_t button,unsigned ticks) {
 capture_buttons=button;ht_input_update(0);for(unsigned i=0;i<ticks;++i)ht_advance(capture_now+=32);
}
int main(int argc,char **argv){
 if(argc!=4)return 2;
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8);
 if(!mem||!bits)return 3;ht_bind(mem);ht_bind_native(mem);ht_reader_bitmap=bits;app=&capture_api;
 memset(&ht,0,sizeof(ht));ht.level=3;ht_select_level(3);ht_spawn(true);ht_camera_mode=HT_CAMERA_NATIVE;
 for(int tick=0;tick<18000 && ht.level==3 && ht.x<1232*256;++tick)walk_route_tick();
 assert(ht.level==3 && !ht.deaths && ht.grounded);
 ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;ht_input(1);
 unsigned phase=(unsigned)atoi(argv[2]);bool approach=!strcmp(argv[1],"approach");
 const char *mode="upper-deck baseline";
#ifdef HT_TRESTLE_ROUTE
 if(!approach) {
  input(T5_APP_BUTTON_CONFIRM,1);input(0,1);assert(ht.traversal.mode==HT_TRESTLE);
  for(unsigned limit=0;limit<1000 && ht.traversal.trestle_phase<phase;++limit) {
   if(ht.traversal.trestle_phase==HT_TRESTLE_TOUCH && phase>HT_TRESTLE_TOUCH && !ht.traversal.trestle_tested) {
    input(0,1);input(T5_APP_BUTTON_CONFIRM,1);input(0,1);
   }
   input(T5_APP_BUTTON_RIGHT,1);
  }
  input(0,1);assert(ht.traversal.trestle_phase==phase);
 }
 mode="lower-brace route";
#else
 /* No lower route exists before. Walk normally to the corresponding world-x
  * on its original upper deck; never manufacture missing baseline artwork. */
 int target=1234;
 if(phase<16)target=1234+(int)phase*12/16;
 else if(phase<80)target=1246;
 else if(phase<194)target=1246+(int)phase-80;
 else if(phase<258)target=1360+((int)phase>242?((int)phase-242)*10/16:0);
 else if(phase<362)target=1370+(int)phase-258;
 else if(phase<426)target=1474;
 else target=1474+((int)phase-426);
 if(!approach)for(unsigned limit=0;limit<1000 && abs(ht.x/256-target)>1;++limit)
  input(ht.x/256<target?T5_APP_BUTTON_RIGHT:T5_APP_BUTTON_LEFT,1);
 input(0,1);
#endif
 assert(ht.level==3 && !ht.deaths && !reading);
 ht.camera=1120*256;ht.camera_y=99*256;ht.intimacy=0;ht.vista=ht.drop_zoom=0;ht.sway_phase=ht.rotation_phase=0;
 ht_render_scene();ht_narration(&ht);ht_traversal_prompt(&ht);
 char file[1024];snprintf(file,sizeof(file),"%s.pgm",argv[3]);FILE *f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P5\n960 540\n255\n");for(int i=0;i<HT_NATIVE_PIXELS;++i)fputc(255-ht_scene[i],f);fclose(f);
 ht_pack_mono(bits,120);snprintf(file,sizeof(file),"%s.pbm",argv[3]);f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P4\n960 540\n");fwrite(bits,1,HT_NATIVE_PIXELS/8,f);fclose(f);
 printf("%s; phase=%u; x=%d y=%d mode=%u evidence=%u deaths=%u\n",mode,phase,ht.x,ht.y,ht.traversal.mode,ht.evidence,ht.deaths);
 free(bits);free(mem);return 0;
}
'''.replace('@APP@',str(r/'Apps/hollow_trail.c')).replace('@ROUTE@',str(r/'test/native_apps/hollow_trail_route_walk.inc'))
captures=[]
with tempfile.TemporaryDirectory(prefix='trestle-preview-') as tmp:
 tmp=pathlib.Path(tmp);c,exe=tmp/'capture.c',tmp/'capture';c.write_text(source)
 subprocess.run(['cc','-std=c11','-O2','-Wno-unused-function','-I'+str(r/'lib/NativeApps/include'),'-I'+str(r/'sdk/driver'),str(c),'-o',str(exe)],check=True)
 sheet=Image.new('RGB',(1440,1210),'#e9e6df');draw=ImageDraw.Draw(sheet)
 draw.text((12,8),'HOLLOW TRAIL | Actual input and production raster | Trestle',fill='#252525')
 for i,(name,phase) in enumerate(shots):
  stem=tmp/name;state=subprocess.check_output([str(exe),name,str(phase),str(stem)],text=True).strip();hashes={}
  for ext,suffix in [('.pgm',''),('.pbm','-mono')]:
   frame=Image.open(stem.with_suffix(ext));frame.save(out/(name+suffix+'.png'))
   hashes['mono' if suffix else 'gray']=hashlib.sha256(frame.tobytes()).hexdigest()
  x,y=i%3*480,30+i//3*295
  sheet.paste(Image.open(stem.with_suffix('.pgm')).resize((480,270),Image.Resampling.LANCZOS).convert('RGB'),(x,y))
  draw.text((x+9,y+274),name+' | '+state.split(';')[0],fill='#252525')
  captures.append({'view':name,'requested_phase':phase,'observed_state':state,'hashes':hashes})
 sheet.save(out/'contact-sheet.png')
meta={'version':json.loads((r/'Apps/hollow_trail.json').read_text())['version'],
 'source_files':{str(f.relative_to(r)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted((r/'Apps').glob('hollow_trail*')) if f.is_file()},
 'fixture':'Input-only rail approach. After A attaches to actual lower members; L/R crosses and fresh A tests the wool-adjacent footing. Before uses matching world-x on its original upper deck. Identical fixed world framing; production raster, no device claim.',
 'raster':[960,540],'captures':captures}
meta.update(source_identity(r,meta['source_files'],a.source_ref))
(out/'capture-metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
if a.compare_before:
 before=a.compare_before;before_meta=json.loads((before/'capture-metadata.json').read_text());pairs=[]
 for name,phase in shots:
  canvas=Image.new('RGB',(1920,1134),'#e9e6df');draw=ImageDraw.Draw(canvas);counts={}
  draw.text((12,8),'BEFORE '+source_caption(before_meta)+' | '+name,fill='#252525')
  draw.text((972,8),'AFTER '+source_caption(meta)+' | '+name,fill='#252525')
  for row,suffix in enumerate(['','-mono']):
   images=[Image.open(folder/(name+suffix+'.png')).convert('RGB') for folder in (before,out)]
   assert all(im.size==(960,540) for im in images)
   for col,im in enumerate(images):
    canvas.paste(im,(960*col,26+560*row))
    assert canvas.crop((960*col,26+560*row,960*col+960,566+560*row)).tobytes()==im.tobytes()
   left,right=(im.tobytes() for im in images)
   counts['mono' if suffix else 'gray']=sum(left[i:i+3]!=right[i:i+3] for i in range(0,len(left),3))
  draw.text((12,570),'Production packed monochrome; original pixels checked byte-for-byte',fill='#252525')
  canvas.save(out/(name+'-before-after.png'));pairs.append({'view':name,'changed_pixels':counts})
 (out/'comparison-metadata.json').write_text(json.dumps({'before':before_meta,'after':meta,'pairs':pairs},indent=2)+'\n')
print(out/'contact-sheet.png')
