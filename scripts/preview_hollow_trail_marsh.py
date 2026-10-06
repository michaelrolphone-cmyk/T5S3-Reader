#!/usr/bin/env python3
"""Capture the actual Chapter V bank, cloud-lit shallows and quarry approach."""
import argparse,hashlib,json,pathlib,subprocess,tempfile
from PIL import Image,ImageDraw
from hollow_trail_capture_source import source_identity,source_caption
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1]);p.add_argument('--source-ref');p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--compare-before',type=pathlib.Path)
a=p.parse_args();r=a.source_root.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
ref=a.source_ref or 'HEAD'
if a.source_ref:subprocess.run(['git','-C',str(r),'diff','--exit-code',ref,'--','Apps'],check=True)
commit=subprocess.check_output(['git','-C',str(r),'rev-parse',ref],text=True).strip()
shots=[('ferry-retained',1,740),('bank-entry',3,1460),('shallows-clear',4,1650),('shallows-mirror',4,1650),('awning-retained',4,1740),('willow-posts',6,2240),('quarry-seam',8,2730),('lock-approach',9,3100)]
source=r'''
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "@APP@"
#include "@ROUTE@"
static uint32_t capture_now,capture_buttons;
static bool capture_poll(t5_app_input_t *out,uint32_t wait){capture_now+=wait;memset(out,0,sizeof(*out));out->buttons=capture_buttons;return true;}
static uint32_t capture_millis(void){return capture_now;}
static const t5_app_api_v1 capture_api={.abi_version=1,.struct_size=sizeof(capture_api),.poll=capture_poll,.millis=capture_millis};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &capture_api;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
int main(int argc,char **argv){
 if(argc!=5)return 2;unsigned extra=(unsigned)atoi(argv[2]);
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8);if(!mem||!bits)return 3;
 ht_bind(mem);ht_bind_native(mem);ht_reader_bitmap=bits;app=&capture_api;
 memset(&ht,0,sizeof(ht));ht.level=4;ht_select_level(4);ht_spawn(true);ht_camera_mode=HT_CAMERA_NATIVE;
 int platform=atoi(argv[1]),target=(int)extra;
 for(unsigned tick=0;tick<20000;++tick){
  if(ht.x/256>=target && ht.grounded && ht.y/256==ht_surface_at(&ht,platform,ht.x/256))break;
  walk_route_tick();
 }
 assert(ht.level==4 && !ht.deaths && ht.x/256>=target && ht.x/256<=target+3);

 for(unsigned i=0;i<24;++i)ht_step_controls(0,0,false,false);
 if(!strncmp(argv[4],"shallows-",9)){unsigned phase=!strcmp(argv[4],"shallows-clear")?0:512;for(unsigned n=0;n<1024 && (ht.ticks&1023u)!=phase;++n)ht_step_controls(0,0,false,false);}
 ht_game frozen=ht;ht_render_scene();assert(!memcmp(&ht,&frozen,sizeof(ht)));

 char file[1024];snprintf(file,sizeof(file),"%s.pgm",argv[3]);FILE *f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P5\n960 540\n255\n");for(int i=0;i<HT_NATIVE_PIXELS;++i)fputc(255-ht_scene[i],f);fclose(f);
 ht_pack_mono(bits,120);snprintf(file,sizeof(file),"%s.pbm",argv[3]);f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P4\n960 540\n");fwrite(bits,1,HT_NATIVE_PIXELS/8,f);fclose(f);
 printf("world level=%u evidence=%u ticks=%u x=%d y=%d camera=%d camera_y=%d\n",ht.level,ht.evidence,ht.ticks,ht.x,ht.y,ht.camera,ht.camera_y);
 free(bits);free(mem);return 0;
}
'''.replace('@APP@',str(r/'Apps/hollow_trail.c')).replace('@ROUTE@',str(r/'test/native_apps/hollow_trail_route_walk.inc'))
captures=[]
with tempfile.TemporaryDirectory(prefix='marsh-preview-') as tmp:
 t=pathlib.Path(tmp);(t/'capture.c').write_text(source)
 subprocess.run(['cc','-std=c11','-O2','-Wno-unused-function','-I'+str(r/'lib/NativeApps/include'),'-I'+str(r/'sdk/driver'),str(t/'capture.c'),'-o',str(t/'capture')],check=True)
 sheet=Image.new('RGB',(1440,1770),'#e9e6df');draw=ImageDraw.Draw(sheet);draw.text((12,8),'HOLLOW TRAIL | Input-only route | Marsh path',fill='#252525')
 for i,(name,choice,extra) in enumerate(shots):
  stem=t/name;state=subprocess.check_output([str(t/'capture'),str(choice),str(extra),str(stem),name],text=True).strip();hashes={}
  for ext,suffix in [('.pgm',''),('.pbm','-mono')]:
   frame=Image.open(stem.with_suffix(ext));frame.save(out/(name+suffix+'.png'));hashes['mono' if suffix else 'gray']=hashlib.sha256(frame.tobytes()).hexdigest()
   sheet.paste(frame.resize((480,270),Image.Resampling.LANCZOS).convert('RGB'),((i%3)*480,30+(i//3)*580+(280 if suffix else 0)))
  draw.text(((i%3)*480+9,305+(i//3)*580),name,fill='#252525');captures.append({'view':name,'target':extra,'observed_state':state,'hashes':hashes})
 sheet.save(out/'contact-sheet.png')
meta={'source_commit':commit,'version':json.loads((r/'Apps/hollow_trail.json').read_text())['version'],'source_files':{str(f.relative_to(r)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted((r/'Apps').glob('hollow_trail*')) if f.is_file()},'fixture':'The input-only Chapter V route rows the original ferry and walks the original bank to each target. Clear and mirror views wait through ordinary neutral simulation to fixed phases of the same cloud cycle. No body, terrain, puzzle or camera state is manufactured. Captures use the production camera throughout.','raster':[960,540],'captures':captures};meta.update(source_identity(r,meta['source_files'],a.source_ref));(out/'capture-metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
if a.compare_before:
 before=a.compare_before;bm=json.loads((before/'capture-metadata.json').read_text());pairs=[]
 for name,choice,extra in shots:
  canvas=Image.new('RGB',(1920,1134),'#e9e6df');draw=ImageDraw.Draw(canvas)
  draw.text((12,8),'BEFORE '+source_caption(bm)+' | '+name,fill='#252525');draw.text((972,8),'AFTER '+source_caption(meta)+' | '+name,fill='#252525')
  changes={}
  for row,suffix in enumerate(['','-mono']):
   frames=[Image.open(folder/(name+suffix+'.png')).convert('RGB') for folder in [before,out]]
   for col,frame in enumerate(frames):
    canvas.paste(frame,(col*960,26+row*560));assert canvas.crop((col*960,26+row*560,col*960+960,566+row*560)).tobytes()==frame.tobytes()
   left,right=(f.tobytes() for f in frames);changes['mono' if suffix else 'gray']=sum(left[i:i+3]!=right[i:i+3] for i in range(0,len(left),3))
  draw.text((12,570),'Production packed monochrome; original pixels verified byte-for-byte',fill='#252525');canvas.save(out/(name+'-before-after.png'));pairs.append({'view':name,'changed_pixels':changes})
 (out/'comparison-metadata.json').write_text(json.dumps({'before':bm,'after':meta,'pairs':pairs},indent=2)+'\n')
print(out/'contact-sheet.png')
