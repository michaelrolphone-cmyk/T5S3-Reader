#include <DisplaySurface.h>

#include <cassert>
#include <cstdint>

static constexpr DisplaySurfaceInfo validMono() {
  return DisplaySurfaceInfo{
      960, 540, 540, 960, 120, 64800, DisplayPixelFormat::Mono1,
      DisplaySafeInsets{9, 3, 9, 3}};
}

static_assert(static_cast<uint8_t>(DisplayPresentMode::Clean) == 0u);
static_assert(static_cast<uint8_t>(DisplayPresentMode::Quality) == 1u);
static_assert(static_cast<uint8_t>(DisplayPresentMode::Balanced) == 2u);
static_assert(static_cast<uint8_t>(DisplayPresentMode::LowLatency) == 3u);
static_assert(validateDisplaySurfaceInfo(validMono()) == DisplaySurfaceValidationError::None);

int main() {
  {
    const auto info = validMono();
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::None);
  }
  {
    auto info = validMono();
    info.width = 0;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidGeometry);
  }
  {
    auto info = validMono();
    info.strideBytes = 119;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidStride);
  }
  {
    auto info = validMono();
    info.bufferSize = 64799;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidBufferSize);
  }
  {
    auto info = validMono();
    info.visibleWidth = 960;
    info.visibleHeight = 960;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidVisibleArea);
  }
  {
    // Area alone fits inside 960x540, but 1000px fits neither physical axis.
    // This must be rejected before panel initialization.
    auto info = validMono();
    info.visibleWidth = 1000;
    info.visibleHeight = 500;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidVisibleArea);
  }
  {
    // A misleadingly small total area is still invalid when neither axis
    // mapping can physically contain it.
    auto info = validMono();
    info.visibleWidth = 1000;
    info.visibleHeight = 100;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidVisibleArea);
  }
  {
    auto info = validMono();
    info.safeInsets.left = 300;
    info.safeInsets.right = 240;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidSafeInsets);
  }
  {
    auto info = validMono();
    info.pixelFormat = DisplayPixelFormat::Rgb565;
    info.strideBytes = 1920;
    info.bufferSize = 1920u * 540u;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::None);
  }
  {
    auto info = validMono();
    info.width = 4097;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidGeometry);
  }
  {
    auto info = validMono();
    info.bufferSize = DISPLAY_SURFACE_MAX_BUFFER_BYTES + 1u;
    assert(validateDisplaySurfaceInfo(info) == DisplaySurfaceValidationError::InvalidBufferSize);
  }
  return 0;
}
