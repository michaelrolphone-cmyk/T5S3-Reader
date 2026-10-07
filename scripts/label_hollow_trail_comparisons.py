#!/usr/bin/env python3
"""Add verified historical code SHAs to caption strips without touching game pixels."""
import hashlib
import json
import pathlib
import subprocess
from PIL import Image, ImageDraw
from hollow_trail_capture_source import source_identity, source_caption
root=pathlib.Path(__file__).resolve().parents[1]
pairs={'mill-register':('b18a5abf','213113ff'),'mill-memory':('213113ff','f42a82d7'),
       'western-view':('213113ff','f42a82d7'),'shutter-entry':('f42a82d7','eeafe0ff'),
       'final-door':('eeafe0ff','8464abdf'),'marked-tree':('8464abdf','1e469466'),
       'sawn-roots':('1e469466','bb636538'),'vanished-house':('bb636538','a585fc98')}
verified=[]
# Validate every historical snapshot before writing anything.
for scene,refs in pairs.items():
    folder=root/'docs/images'/('hollow-trail-'+scene)
    comparison=next(folder.rglob('comparison-metadata.json'))
    meta=json.loads(comparison.read_text())
    for side,ref in zip(('before','after'),refs):
        meta[side].update(source_identity(root,meta[side]['source_files'],ref))
    verified.append((scene,folder,comparison,meta))
for scene,folder,comparison,meta in verified:
    regions=[]
    for path in sorted(folder.rglob('*before-after*.png')):
        image=Image.open(path).convert('RGB');old=image.copy()
        top=30 if scene=='final-door' else 26
        assert image.width==1920
        crops=[(0,top,1920,top+540)]
        if scene!='final-door': crops.append((0,586,1920,1126))
        original=[image.crop(box).tobytes() for box in crops]
        name=path.stem.replace('-before-after','')
        draw=ImageDraw.Draw(image);draw.rectangle((0,0,1919,top-1),fill='#e9e6df')
        draw.text((12,8),'BEFORE '+source_caption(meta['before'])+' | '+name,fill='#252525')
        draw.text((972,8),'AFTER '+source_caption(meta['after'])+' | '+name,fill='#252525')
        for box,data in zip(crops,original):assert image.crop(box).tobytes()==data
        # Strictly stronger: the entire image below the caption strip is unchanged.
        assert image.crop((0,top,1920,image.height)).tobytes()==old.crop((0,top,1920,old.height)).tobytes()
        image.save(path)
        regions.append({'file':str(path.relative_to(folder)),'unchanged_below_row':top,
                        'raster_sha256':[hashlib.sha256(data).hexdigest() for data in original]})
    meta['caption_verification']={'method':'Only top caption strip changed; every pixel below it remains byte-identical.', 'images':regions}
    comparison.write_text(json.dumps(meta,indent=2)+'\n')
    for side in ('before','after'):
        p=folder/side/'capture-metadata.json'
        if p.exists():p.write_text(json.dumps(meta[side],indent=2)+'\n')
    readme=folder/'README.md';text=readme.read_text()
    note='\nComparison captions identify both the app version and the verified source commit.\nThe shared 1.1.49 version is one unreleased update containing multiple source\ncheckpoints, not a claim that both pictures show identical code.\n'
    if 'Comparison captions identify both' not in text:readme.write_text(text+note)
    print(scene, len(regions), source_caption(meta['before']), '->', source_caption(meta['after']))
