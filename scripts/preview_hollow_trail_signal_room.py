#!/usr/bin/env python3
"""Capture the real signal-room renderer, or the corresponding baseline view.

Use the SAME script with --source-root pointing at each checkout. Requires cc
and Pillow. Stores original 960x540 grayscale and production packed-mono frames,
plus source/camera/tick metadata. Baseline study is the real compact journal
fallback (no typography provider on host), explicitly recorded in metadata.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile
from PIL import Image, ImageDraw

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1])
parser.add_argument('--output',type=pathlib.Path,required=True)
parser.add_argument('--source-ref',help='Verified provenance commit; its entire Apps tree must match the source checkout')
args=parser.parse_args();repo=args.source_root.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
source_ref=args.source_ref or 'HEAD'
if args.source_ref:subprocess.run(['git','-C',str(repo),'diff','--exit-code',source_ref,'--','Apps'],check=True)
source_commit=subprocess.check_output(['git','-C',str(repo),'rev-parse',source_ref],text=True).strip()
shots=[('window-lit',8,'Window approach / light on'),('window-dark',80,'Window approach / long pause'),
       ('room-log',8,'Log beside the empty chair'),('room-wide',80,'Existing roof / wide room view'),
       ('refuge-sightline',8,'Same tank-to-window sightline'),
       ('study-room',8,'Inspect: autonomous wheel and occupied chair'),
       ('study-separated',8,'Inspect: two separate watch-log pages'),
       ('study-aligned',8,'Inspect: matching signature blots at the window'),
       ('arrival-hold',232,'Earned arrival / watching the mechanism'),
       ('arrival-handoff',576,'Control returns on the same roof'),
       ('read-log',8,'Read log: novella passage in the existing journal')]
source=r'''
#include <stdio.h>
#include <stdlib.h>
#include "@APP@"
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return NULL;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
int main(int argc,char **argv){
 if(argc!=4)return 2;
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8);
 if(!mem||!bits)return 3;ht_bind(mem);ht_bind_native(mem);ht_reader_bitmap=bits;
 memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);ht_camera_mode=HT_CAMERA_NATIVE;
 const char *shot=argv[1];unsigned tick=(unsigned)atoi(argv[2]);
 bool study=!strncmp(shot,"study-",6),refuge=!strcmp(shot,"refuge-sightline");
 bool arrival=!strncmp(shot,"arrival-",8);
 int x=refuge?1840:study?ht_evidence_x(1,2):arrival?2090:!strncmp(shot,"window-",7)?2098:2170;
 ht.story_x=x;ht.x=x*256;ht.y=(refuge?70:-100)*256;ht.camera=(x-200)*256;ht.camera_y=(ht.y/256-180)*256;
 ht.grounded=true;ht.ticks=arrival?8u:tick;ht.scene_evidence=2;ht.weather_amount=256;ht.weather_age=96;
 ht.intimacy=!strcmp(shot,"room-wide")?0:256;ht.vista=!strcmp(shot,"room-wide")?256:0;
 bool journal=false;
 if(!strcmp(shot,"read-log")){ht.evidence|=1u<<5;ht_journal_open(5);ht_journal_render();journal=true;}
 else if(refuge){ht_cutscene_begin(HT_CUTSCENE_RAIN);ht_cutscene.tick=170;ht_cutscene_render(&ht_cutscene);}
 else if(study){
  (void)ht_inspect();
#ifdef HT_SIGNAL_ROOM_HEIGHT
  unsigned focus=!strcmp(shot,"study-room")?0:!strcmp(shot,"study-separated")?1:2;
  ht_signal_room_study_render(focus,tick);
#else
  ht_journal_open(5);ht_journal_render();journal=true;
#endif
 }else{
#ifdef HT_SIGNAL_ROOM_HEIGHT
  if(arrival){
   ht_cutscene_begin(HT_CUTSCENE_SIGNAL);ht_cutscene.tick=(uint16_t)tick;
   if(tick<576)ht_cutscene_render(&ht_cutscene);
   else{ht_cutscene.active=false;ht_cutscene_apply_handoff(&ht_cutscene);ht_render_scene();ht_narration(&ht);}
  }else
#endif
  {
#ifndef HT_SIGNAL_ROOM_HEIGHT
   if(arrival && tick<576)ht.ticks+=tick;
#endif
   ht_render_scene();ht_narration(&ht);}
 }
 char file[1024];snprintf(file,sizeof(file),"%s.pgm",argv[3]);FILE *f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P5\n960 540\n255\n");
 for(int y=0;y<540;++y)for(int x0=0;x0<960;++x0)fputc(255-ht_scene[journal?(y/2)*480+x0/2:y*960+x0],f);fclose(f);
 if(!journal)ht_pack_mono(bits,120);
 snprintf(file,sizeof(file),"%s.pbm",argv[3]);f=fopen(file,"wb");if(!f)return 4;
 fprintf(f,"P4\n960 540\n");fwrite(bits,1,HT_NATIVE_PIXELS/8,f);fclose(f);
 free(bits);free(mem);return 0;
}
'''.replace('@APP@',str(repo/'Apps/hollow_trail.c'))
with tempfile.TemporaryDirectory(prefix='signal-room-preview-') as tmp:
 src=pathlib.Path(tmp)/'preview.c';src.write_text(source);binary=pathlib.Path(tmp)/'preview'
 subprocess.run(['cc','-std=c11','-O2','-Wno-unused-function','-I'+str(repo/'lib/NativeApps/include'),'-I'+str(repo/'sdk/driver'),str(src),'-o',str(binary)],check=True)
 sheet=Image.new('RGB',(1440,30+((len(shots)+2)//3)*295),'#e9e6df');draw=ImageDraw.Draw(sheet)
 version=json.loads((repo/'Apps/hollow_trail.json').read_text())['version']
 draw.text((16,8),f'HOLLOW TRAIL {version} | Actual C-rendered frames | No physical device claim',fill='#252525')
 captures=[]
 for i,(name,tick,label) in enumerate(shots):
  stem=pathlib.Path(tmp)/name;subprocess.run([str(binary),name,str(tick),str(stem)],check=True)
  frame=Image.open(stem.with_suffix('.pgm'));frame.save(out/(name+'.png'))
  mono=Image.open(stem.with_suffix('.pbm'));mono.save(out/(name+'-mono.png'))
  x,y=i%3*480,30+i//3*295;sheet.paste(frame.resize((480,270),Image.Resampling.LANCZOS).convert('RGB'),(x,y));draw.text((x+9,y+274),label,fill='#252525')
  captures.append({'shot':name,'tick':tick,'rain_tableau_tick':170 if name=='refuge-sightline' else None,'width':960,'height':540,'gray_sha256':hashlib.sha256(frame.tobytes()).hexdigest(),'mono_sha256':hashlib.sha256(mono.tobytes()).hexdigest()})
 sheet.save(out/'contact-sheet.png')
 paths=sorted((repo/'Apps').glob('hollow_trail*'))
 metadata={'source_commit':source_commit,'version':version,'signal_room_present':(repo/'Apps/hollow_trail_signal_room.inc').exists(),'baseline_study_note':'Actual journal compact fallback because host has no reader.typography provider','source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()},'captures':captures}
 (out/'capture-metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(out/'contact-sheet.png')
