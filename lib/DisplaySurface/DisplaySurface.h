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

  // Transitional source-compatibility aliases. New code uses the hardware-
  // neutral names above; remove these after legacy UI callers migrate.
  FULL_REFRESH = Clean,
  HALF_REFRESH = Quality,
  BALANCED_REFRESH = Balanced,
  FAST_REFRESH = LowLatency,
};

enum class DisplayEffect : uint8_t {
  None = 0,
  PageTurnForwardStandard = 1,
  PageTurnBackwardStandard = 2,
  PageTurnForwardFast = 3,
  PageTurnBackwardFast = 4,

  // Transitional aliases for existing HalDisplay-scoped call sites.
  EFFECT_NONE = None,
  EFFECT_READER_TURN_FORWARD_STANDARD = PageTurnForwardStandard,
  EFFECT_READER_TURN_BACKWARD_STANDARD = PageTurnBackwardStandard,
  EFFECT_READER_TURN_FORWARD_FAST = PageTurnForwardFast,
  EFFECT_READER_TURN_BACKWARD_FAST = PageTurnBackwardFast,
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

enum class DisplaySurfaceValidationError : uint8_t {
  None = 0,
  UnsupportedPixelFormat,
  InvalidGeometry,
  InvalidStride,
  InvalidBufferSize,
  InvalidVisibleArea,
  InvalidSafeInsets,
};

static constexpr uint32_t DISPLAY_SURFACE_MAX_DIMENSION = 4096u;
static constexpr uint32_t DISPLAY_SURFACE_MAX_BUFFER_BYTES = 16u * 1024u * 1024u;

inline uint8_t displayPixelFormatBitsPerPixel(const DisplayPixelFormat format) {
  switch (format) {
    case DisplayPixelFormat::Mono1: return 1;
    case DisplayPixelFormat::Gray2: return 2;
    case DisplayPixelFormat::Gray4: return 4;
    case DisplayPixelFormat::Gray8: return 8;
    case DisplayPixelFormat::Rgb565: return 16;
  }
  return 0;
}

inline const char* displaySurfaceValidationErrorName(const DisplaySurfaceValidationError error) {
  switch (error) {
    case DisplaySurfaceValidationError::None: return "none";
    case DisplaySurfaceValidationError::UnsupportedPixelFormat: return "unsupported-pixel-format";
    case DisplaySurfaceValidationError::InvalidGeometry: return "invalid-geometry";
    case DisplaySurfaceValidationError::InvalidStride: return "invalid-stride";
    case DisplaySurfaceValidationError::InvalidBufferSize: return "invalid-buffer-size";
    case DisplaySurfaceValidationError::InvalidVisibleArea: return "invalid-visible-area";
    case DisplaySurfaceValidationError::InvalidSafeInsets: return "invalid-safe-insets";
  }
  return "unknown";
}

inline DisplaySurfaceValidationError validateDisplaySurfaceInfo(const DisplaySurfaceInfo& info) {
  const uint8_t bitsPerPixel = displayPixelFormatBitsPerPixel(info.pixelFormat);
  if (bitsPerPixel == 0) return DisplaySurfaceValidationError::UnsupportedPixelFormat;

  if (info.width == 0 || info.height == 0 || info.visibleWidth == 0 || info.visibleHeight == 0 ||
      info.width > DISPLAY_SURFACE_MAX_DIMENSION || info.height > DISPLAY_SURFACE_MAX_DIMENSION ||
      info.visibleWidth > DISPLAY_SURFACE_MAX_DIMENSION || info.visibleHeight > DISPLAY_SURFACE_MAX_DIMENSION) {
    return DisplaySurfaceValidationError::InvalidGeometry;
  }

  const uint64_t minimumStride =
      (static_cast<uint64_t>(info.width) * static_cast<uint64_t>(bitsPerPixel) + 7u) / 8u;
  if (info.strideBytes < minimumStride) return DisplaySurfaceValidationError::InvalidStride;

  const uint64_t requiredBytes = static_cast<uint64_t>(info.strideBytes) * info.height;
  if (requiredBytes == 0 || requiredBytes > DISPLAY_SURFACE_MAX_BUFFER_BYTES ||
      info.bufferSize < requiredBytes || info.bufferSize > DISPLAY_SURFACE_MAX_BUFFER_BYTES) {
    return DisplaySurfaceValidationError::InvalidBufferSize;
  }

  const uint64_t visibleArea = static_cast<uint64_t>(info.visibleWidth) * info.visibleHeight;
  const uint64_t physicalArea = static_cast<uint64_t>(info.width) * info.height;
  if (visibleArea > physicalArea) return DisplaySurfaceValidationError::InvalidVisibleArea;

  if (static_cast<uint32_t>(info.safeInsets.left) + info.safeInsets.right >= info.visibleWidth ||
      static_cast<uint32_t>(info.safeInsets.top) + info.safeInsets.bottom >= info.visibleHeight) {
    return DisplaySurfaceValidationError::InvalidSafeInsets;
  }

  return DisplaySurfaceValidationError::None;
}

class DisplaySurface {
 public:
  virtual ~DisplaySurface() = default;

  virtual bool isReady() const = 0;
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
