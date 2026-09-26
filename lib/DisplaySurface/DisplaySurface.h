#pragma once

#include <cstddef>
#include <cstdint>

// Hardware-neutral software surface contract used by RiscRTE rendering.
// Physical display ownership remains in the active display provider. The
// current HalDisplay implementation is a compatibility backend for this
// interface until display hardware moves into provider ELFs.
enum class DisplayPixelFormat : uint8_t {
  Mono1 = 1,
  Gray2 = 2,
  Gray4 = 3,
  Gray8 = 4,
  Rgb565 = 5,
};

enum class DisplayPresentMode : uint8_t {
  Clean = 0,
  Quality = 1,
  Balanced = 2,
  LowLatency = 3,
};

enum class DisplayEffect : uint8_t {
  None = 0,
  PageTurnForwardStandard = 1,
  PageTurnBackwardStandard = 2,
  PageTurnForwardFast = 3,
  PageTurnBackwardFast = 4,
};

struct DisplaySafeInsets {
  uint16_t top = 0;
  uint16_t right = 0;
  uint16_t bottom = 0;
  uint16_t left = 0;
};

struct DisplaySurfaceInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t visibleWidth = 0;
  uint16_t visibleHeight = 0;
  uint16_t strideBytes = 0;
  uint32_t bufferSize = 0;
  DisplayPixelFormat pixelFormat = DisplayPixelFormat::Mono1;
  DisplaySafeInsets safeInsets{};
};

class DisplaySurface {
 public:
  virtual ~DisplaySurface() = default;

  virtual DisplaySurfaceInfo getSurfaceInfo() const = 0;
  virtual uint8_t* getFrameBuffer() const = 0;

  virtual void clearScreen(uint8_t color = 0xFF) const = 0;
  virtual void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         bool fromProgmem = false) const = 0;
  virtual void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                    bool fromProgmem = false) const = 0;

  // Synchronous compatibility path. The provider-facing display.output ABI is
  // asynchronous; this keeps existing firmware behavior during migration.
  virtual void displayBuffer(DisplayPresentMode mode = DisplayPresentMode::LowLatency,
                             bool turnOffScreen = false) = 0;
  virtual void requestNextRefresh(DisplayPresentMode mode = DisplayPresentMode::Quality) = 0;
  virtual void requestNextDisplayEffect(DisplayEffect effect = DisplayEffect::None) = 0;

  // Existing reader grayscale-plane compatibility. This is deliberately not
  // part of the provider ABI; color providers negotiate a native pixel format.
  virtual void copyGrayscaleLsbBuffers(const uint8_t* buffer) = 0;
  virtual void copyGrayscaleMsbBuffers(const uint8_t* buffer) = 0;
  virtual bool captureGrayscaleBaseBuffer(const uint8_t* buffer) = 0;
  virtual void cleanupGrayscaleBuffers(const uint8_t* buffer) = 0;
  virtual void displayGrayBuffer(DisplayPresentMode mode = DisplayPresentMode::Quality) = 0;
};
