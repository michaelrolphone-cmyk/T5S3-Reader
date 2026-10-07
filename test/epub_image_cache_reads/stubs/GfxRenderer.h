#pragma once
#include <iterator>
#include <algorithm>
#include <cstdint>
class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_MSB, GRAYSCALE_LSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  uint8_t framebuffer[960*540/8]{};
  RenderMode mode=BW; Orientation orientation=Portrait;
  int getScreenWidth()const{return orientation==Portrait||orientation==PortraitInverted?540:960;}
  int getScreenHeight()const{return orientation==Portrait||orientation==PortraitInverted?960:540;}
  int getDisplayWidth()const{return 960;}
  int getDisplayHeight()const{return 540;}
  int getDisplayWidthBytes()const{return 120;}
  Orientation getOrientation()const{return orientation;}
  RenderMode getRenderMode()const{return mode;}
  uint8_t* getFrameBuffer(){return framebuffer;}
  void clear(){std::fill(std::begin(framebuffer),std::end(framebuffer),mode==BW?0xff:0x00);}
};
