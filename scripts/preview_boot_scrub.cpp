// Host preview + endpoint invariant check for the production display waveform.
// c++ -std=c++17 -O2 scripts/preview_boot_scrub.cpp -o /tmp/preview-boot-scrub
// /tmp/preview-boot-scrub OUTPUT_DIRECTORY
#include "../src/native/NativeVideoBootScrub.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace NativeVideoBootScrub;
int main(int argc, char** argv) {
  assert(argc == 2);
  std::vector<uint8_t> map(kMapBytes), image(kMapBytes, 255), drive(kWidth / 4);
  for (unsigned y=0; y<kHeight; y+=kGrid) buildBand(map.data(), y);
  // Representative retained page; unknown retained pixels need not be read by firmware.
  for (unsigned y=0; y<kHeight; ++y) for (unsigned x=0; x<kWidth; ++x) {
    const unsigned px=y, py=kWidth-1-x;
    if ((py>140 && py<770 && py%32<3 && px>60 && px<480) ||
        (py>80 && py<100 && px>60 && px<340)) image[y*kWidth+x]=45;
  }
  const auto writeFrame = [&](unsigned frame) {
    FILE* f=fopen((std::string(argv[1])+"/frame-"+std::to_string(frame)+".pgm").c_str(),"wb");
    assert(f); fprintf(f,"P5\n540 960\n255\n");
    for(unsigned py=0;py<960;++py)for(unsigned px=0;px<540;++px) fputc(image[px*960+959-py],f);
    fclose(f);
  };
  writeFrame(0);
  unsigned black[kLastArrival+1]={}, white[kLastArrival+1]={};
  for(unsigned scan=0;scan<kScans;++scan) {
    for(unsigned arrival=0;arrival<=kLastArrival;++arrival) {
      black[arrival]+=command(arrival,scan)==1; white[arrival]+=command(arrival,scan)==2;
    }
    unsigned blackPixels=0;
    for(unsigned y=0;y<kHeight;++y) {
      driveRow(map.data(),y,scan,drive.data());
      for(unsigned x=0;x<kWidth;++x) {
        auto command=(drive[x/4]>>(6-2*(x%4)))&3;
        assert(command!=3); assert(map[y*kWidth+x]<=kLastArrival);
        if(command==1) image[y*kWidth+x]=0;
        if(command==2) image[y*kWidth+x]=255;
        blackPixels+=image[y*kWidth+x]==0;
      }
    }
    assert(blackPixels<kMapBytes*3/4); // No near-global black flash.
    writeFrame(scan+1);
  }
  for(unsigned arrival=0;arrival<=kLastArrival;++arrival) {
    assert(black[arrival]==kBlackScans && white[arrival]==kWhiteScans);
    assert(command(arrival,kScans)==0);
  }
  for(auto p:image) assert(p==255);
  puts("PASS: full-screen coverage, bounded balanced endpoint sequence, retain outside sweep, final white");
}
