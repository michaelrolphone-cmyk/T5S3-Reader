#pragma once
#include <algorithm>
#include <cstdint>
class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_MSB, GRAYSCALE_LSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  uint8_t framebuffer[64 * 64 / 8]{};
  RenderMode mode = BW;
  int getScreenWidth() const { return 64; }
  int getScreenHeight() const { return 64; }
  int getDisplayWidth() const { return 64; }
  int getDisplayHeight() const { return 64; }
  int getDisplayWidthBytes() const { return 8; }
  Orientation getOrientation() const { return LandscapeCounterClockwise; }
  RenderMode getRenderMode() const { return mode; }
  uint8_t* getFrameBuffer() { return framebuffer; }
  void clear() { std::fill(std::begin(framebuffer), std::end(framebuffer), mode == BW ? 0xff : 0x00); }
};
