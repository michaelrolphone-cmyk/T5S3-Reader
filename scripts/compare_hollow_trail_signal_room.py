#!/usr/bin/env python3
"""Assemble lossless before/after C-raster evidence; never invent image pixels."""
import argparse
import json
import pathlib
import shutil
from PIL import Image, ImageDraw

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--before',type=pathlib.Path,required=True)
parser.add_argument('--after',type=pathlib.Path,required=True)
parser.add_argument('--output',type=pathlib.Path,required=True)
args=parser.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
metadata=[]
for side,source in [('before',args.before),('after',args.after)]:
    (out/side).mkdir(exist_ok=True)
    for path in source.glob('*.png'):shutil.copyfile(path,out/side/path.name)
    shutil.copyfile(source/'capture-metadata.json',out/side/'capture-metadata.json')
    metadata.append(json.loads((source/'capture-metadata.json').read_text()))
pairs=[]
for capture in metadata[1]['captures']:
    name=capture['shot'];sheet=Image.new('RGB',(1920,1134),'#e9e6df');draw=ImageDraw.Draw(sheet)
    for col,(label,data) in enumerate(zip(['BEFORE','AFTER'],metadata)):
        draw.text((12+col*960,8),f"{label}: {data['version']} / {data['source_commit'][:12]} | {name}",fill='#252525')
    metrics={}
    for row,suffix in enumerate(['','-mono']):
        images=[Image.open(source/(name+suffix+'.png')).convert('RGB') for source in (args.before,args.after)]
        assert all(image.size==(960,540) for image in images)
        for col,image in enumerate(images):sheet.paste(image,(col*960,26+row*560))
        draw.text((12,570),'Production packed monochrome (same full frame and packer)',fill='#252525')
        left,right=(image.tobytes() for image in images)
        metrics['mono' if suffix else 'gray']=sum(left[i:i+3]!=right[i:i+3] for i in range(0,len(left),3))
        for col,image in enumerate(images):assert sheet.crop((col*960,26+row*560,col*960+960,566+row*560)).tobytes()==image.tobytes()
    sheet.save(out/(name+'-before-after.png'));pairs.append({'shot':name,'tick':capture['tick'],'changed_pixels':metrics})
result={'before_source_commit':metadata[0]['source_commit'],'after_source_commit':metadata[1]['source_commit'],'width':960,'height':540,'pairs':pairs,'method':'Full, unscaled C-renderer frames, grayscale above and packed mono below. Comparison canvas crops verified byte-for-byte against originals.'}
(out/'comparison-metadata.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'{len(pairs)} grayscale + mono frame pairs verified in {out}')
