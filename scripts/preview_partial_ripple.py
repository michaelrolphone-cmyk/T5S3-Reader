#!/usr/bin/env python3
"""Preview actual partial-refresh commands; pigment response is illustrative."""
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
CPP = r'''
#include "M5WispRefresh.h"
#include <cstdio>
#include <vector>
int main() {
  using namespace M5WispRefresh;
  std::vector<uint8_t> map(kMapBytes);
  for (auto shape : {std::pair<unsigned,unsigned>{240,130},{260,30}}) {
    const unsigned w=shape.first,h=shape.second;
    for(unsigned y=0;y<h;y+=h>=64?kGrid:1) buildRegionBand(map.data(),0,0,w,h,y);
    for(unsigned scan=0;scan<kScans+kDrawScans;++scan)
      for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        // Emit all four target tones; Python chooses the desired target.
        for(unsigned tone=0;tone<4;++tone)
          std::putchar(rippleCommand(map[y*kWidth+x],scan,tone));
      }
  }
}
'''

def main(destination):
    with tempfile.TemporaryDirectory() as temp:
        source=Path(temp)/'preview.cpp';source.write_text(CPP)
        binary=Path(temp)/'preview'
        subprocess.run(['c++','-std=c++17','-O2','-I'+str(ROOT/'src/native'),
                        '-I'+str(ROOT/'lib/hal'),str(source),'-o',str(binary)],check=True)
        commands=subprocess.check_output([str(binary)],timeout=10)
    font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
    page=Image.new('L',(360,320),245);draw=ImageDraw.Draw(page)
    draw.text((20,10),'CENTER-OUT PARTIAL RIPPLE',font=font,fill=0)
    for y in range(45,285,20):
        draw.text((12,y),'Retained page  /  text and details',font=font,fill=85)
    draw.rectangle((0,290,360,320),fill=245)
    draw.text((12,297),'Command preview • ~0.8 seconds',font=font,fill=0)
    regions=[(60,65,240,130),(50,235,260,30)]
    targets=[]
    for i,(_,_,w,h) in enumerate(regions):
        target=Image.new('L',(w,h),255);d=ImageDraw.Draw(target)
        d.text((12,8),'Updated region' if i==0 else 'Updated narrow strip',font=font,fill=0)
        if i==0:
            d.line((12,40,w-12,40),fill=170,width=2)
            d.text((12,54),'Content follows the wave',font=font,fill=85)
            d.text((12,80),'Neighbors stay unchanged',font=font,fill=0)
        targets.append(target)
    frames=[page.copy()];offsets=[0,240*130*19*4]
    for scan in range(19):
        for i,(ox,oy,w,h) in enumerate(regions):
            target=targets[i];data=page.load();tones=target.load()
            offset=offsets[i]+scan*w*h*4
            for y in range(h):
                for x in range(w):
                    tone=(255-tones[x,y])//85
                    command=commands[offset+(y*w+x)*4+tone]
                    # Illustrative pulse accumulation, not a measured panel model.
                    if command==1:data[ox+x,oy+y]=max(0,data[ox+x,oy+y]-85)
                    if command==2:data[ox+x,oy+y]=min(255,data[ox+x,oy+y]+43)
        frames.append(page.copy())
    frames[0].save(destination,save_all=True,append_images=frames[1:],
                   duration=[600]+[42]*18+[1000],loop=0)
    print(destination)

if __name__=='__main__':
    main(sys.argv[1] if len(sys.argv)>1 else '/tmp/partial-ripple.gif')
