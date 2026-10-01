#!/usr/bin/env python3
"""Run native face/bounds checks and make a sheet from production scanlines.

Pillow is needed only for the preview sheet. Captions approximate the firmware
UI font; the faces themselves use the production pixels, with no smoothing.
"""
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
names = ['Digital segments', 'Smooth sans', 'Classic serif', 'Minimal dial', 'Railway', 'Art Deco']
font_path = str(ROOT / 'lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf')
with tempfile.TemporaryDirectory() as temp:
    subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'src'),
                    str(ROOT/'scripts/preview_clock_faces.cpp'), '-o', temp+'/render'], check=True)
    subprocess.run([temp+'/render', temp], check=True)
    sheet = Image.new('RGB', (1640, 1630), '#e5e5e5')
    for i, name in enumerate(names):
        frame = Image.new('RGB', (800, 480), 'white')
        draw = ImageDraw.Draw(frame)
        svg = ET.parse(Path(temp)/f'clock-800-{i}.svg')
        for element in svg.getroot().iter():
            attrs = element.attrib
            if element.tag.endswith('rect') and attrs.get('width') != '100%':
                x, y, w, h = (int(attrs[k]) for k in ('x','y','width','height'))
                if w and h: draw.rectangle((x,y,x+w-1,y+h-1), fill=attrs['fill'])
        for y, text, size in [(32, '2026-09-30', 24), (402, 'AM', 24), (436, 'Press power button to wake',16)]:
            draw.text((400,y), text, font=ImageFont.truetype(font_path,size), fill='black', anchor='mt')
        x, y = 10+(i%2)*820, 10+(i//2)*540
        ImageDraw.Draw(sheet).text((x+10,y), name, font=ImageFont.truetype(font_path,24), fill='black')
        sheet.paste(frame, (x,y+40))
    dest = ROOT/'docs/previews/desk-clock-faces.png'
    dest.parent.mkdir(exist_ok=True)
    sheet.save(dest)
    print(f'PASS: all faces, 800x480 and 960x540, every minute in 12/24h; preview: {dest}')
