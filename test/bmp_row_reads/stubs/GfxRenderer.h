#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <Arduino.h>
class Bitmap;
struct FontCacheManager {bool scanning=false;bool isScanning()const{return scanning;}};
inline unsigned pixelMillisEvery=0;
class GfxRenderer {
 public:
 enum RenderMode {BW, GRAYSCALE_MSB, GRAYSCALE_LSB};
 FontCacheManager* fontCacheManager_=nullptr;
 RenderMode renderMode=BW;
 int width=540,height=960;
 mutable uint64_t pixelCalls=0;
 mutable std::vector<uint8_t> pixels;
 int getScreenWidth()const{return width;}
 int getScreenHeight()const{return height;}
 void clear(){pixels.assign(static_cast<size_t>(width)*height,0);pixelCalls=0;}
 void drawPixel(int x,int y,bool state=true)const{
  assert(x>=0&&x<width&&y>=0&&y<height);pixels[static_cast<size_t>(y)*width+x]=state?1:2;
  ++pixelCalls;if(pixelMillisEvery&&pixelCalls%pixelMillisEvery==0)++clockMs;
 }
 void drawBitmap(const Bitmap&,int,int,int=0,int=0,float=0,float=0)const;
 void drawBitmap1Bit(const Bitmap&,int,int,int=0,int=0)const;
};
