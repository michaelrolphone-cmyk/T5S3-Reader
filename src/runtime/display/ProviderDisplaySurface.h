#pragma once

#include <DisplaySurface.h>
#include <RiscDisplayOutputV1.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Software raster surface for the existing Reader renderer. The ELF owns every
// physical panel operation; this adapter owns only stable RAM and ABI transfer.
// Reader MONO1 is 0=black, display.output MONO1 is 1=black.
class ProviderDisplaySurface final : public DisplaySurface {
 public:
  ProviderDisplaySurface(const risc_display_output_api_v1* output, uint8_t* raster,
                         size_t rasterSize, uint16_t logicalWidth, uint16_t logicalHeight,
                         uint32_t timeoutMs = 20000)
      : api_(output), raster_(raster), rasterSize_(rasterSize), timeoutMs_(timeoutMs) {
    if (!api_ || api_->api_version != RISC_DISPLAY_OUTPUT_API_V1 ||
        api_->struct_size < sizeof(*api_) || !api_->get_info || !api_->acquire ||
        !api_->release || !api_->submit || !api_->wait_present || !raster_ ||
        timeoutMs_ == 0) return;
    risc_display_info_v1 physical{};
    if (!api_->get_info(api_->context, &physical) ||
        physical.api_version != RISC_DISPLAY_OUTPUT_API_V1 ||
        physical.struct_size < sizeof(physical) ||
        physical.width > UINT16_MAX || physical.height > UINT16_MAX ||
        physical.width == 0 || (physical.width & 7u) ||
        !(physical.supported_formats & RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1))) return;
    const uint64_t bytes = static_cast<uint64_t>(physical.width / 8u) * physical.height;
    if (bytes != rasterSize_ || bytes > UINT32_MAX) return;
    info_ = {static_cast<uint16_t>(physical.width), static_cast<uint16_t>(physical.height),
             logicalWidth, logicalHeight, static_cast<uint16_t>(physical.width / 8u),
             static_cast<uint32_t>(bytes), DisplayPixelFormat::Mono1,
             {physical.safe_insets.top, physical.safe_insets.right,
              physical.safe_insets.bottom, physical.safe_insets.left}};
    sleepSupported_ = (physical.flags & RISC_DISPLAY_INFO_QUIESCE_SLEEP) != 0;
    ready_ = validateDisplaySurfaceInfo(info_) == DisplaySurfaceValidationError::None;
  }

  bool isReady() const override { return ready_; }
  bool supportsSleep() const { return ready_ && sleepSupported_; }
  DisplaySurfaceInfo getSurfaceInfo() const override { return info_; }
  uint8_t* getFrameBuffer() const override { return ready_ ? raster_ : nullptr; }
  bool lastPresentSucceeded() const override { return lastPresentSucceeded_; }
  uint8_t lastPresentState() const { return lastPresentState_; }
  // A boot splash must not be mistaken for the subsequent Home present.
  void clearPresentStatus() { lastPresentSucceeded_ = false; lastPresentState_ = 0; }

  void setFlipOutput(bool enabled) {
    if (flipOutput_ == enabled) return;
    flipOutput_ = enabled;
    // Retain this clean request through failed admission/presentation. A later
    // ordinary UI refresh must not downgrade a whole-screen rotation.
    flipRefreshPending_ = true;
  }

  void clearScreen(uint8_t color = 0xFF) const override {
    if (ready_) std::memset(raster_, color, rasterSize_);
  }
  void drawImage(const uint8_t* image, uint16_t x, uint16_t y, uint16_t width,
                 uint16_t height, bool = false) const override {
    if (!ready_ || !image || x >= info_.width || y >= info_.height || (x & 7u) || (width & 7u)) return;
    const size_t sourceStride = width / 8u;
    for (uint32_t row = 0; row < height && y + row < info_.height; ++row) {
      for (size_t col = 0; col < sourceStride && x / 8u + col < info_.strideBytes; ++col)
        raster_[(y + row) * info_.strideBytes + x / 8u + col] = image[row * sourceStride + col];
    }
  }
  void drawImageTransparent(const uint8_t* image, uint16_t x, uint16_t y,
                            uint16_t width, uint16_t height, bool = false) const override {
    if (!ready_ || !image || x >= info_.width || y >= info_.height || (x & 7u) || (width & 7u)) return;
    const size_t sourceStride = width / 8u;
    for (uint32_t row = 0; row < height && y + row < info_.height; ++row) {
      for (size_t col = 0; col < sourceStride && x / 8u + col < info_.strideBytes; ++col)
        raster_[(y + row) * info_.strideBytes + x / 8u + col] &= image[row * sourceStride + col];
    }
  }

  void suppressInitialFullRefresh() { flipRefreshPending_ = false; }
  void displayBufferDiff(const uint8_t* previous, DisplayPresentMode mode) {
    present(mode, previous);
  }
  void displayBuffer(DisplayPresentMode mode = DisplayPresentMode::LowLatency,
                     bool = false) override { present(mode, nullptr); }
  void present(DisplayPresentMode mode, const uint8_t* previous) {
    lastPresentSucceeded_ = false;
    lastPresentState_ = 0;
    if (!ready_) return;
    risc_display_surface_v1 target{};
    if (!api_->acquire(api_->context, RISC_DISPLAY_FORMAT_MONO1, &target)) return;
    const bool compatible = target.frame != RISC_DISPLAY_FRAME_INVALID && target.pixels &&
        target.pixels != raster_ &&
        target.width == info_.width && target.height == info_.height &&
        target.stride_bytes == info_.strideBytes && target.size_bytes == rasterSize_ &&
        target.pixel_format == RISC_DISPLAY_FORMAT_MONO1;
    if (!compatible) { api_->release(api_->context, target.frame); return; }
    uint8_t* pixels = static_cast<uint8_t*>(target.pixels);
    const auto* history = previous && !flipRefreshPending_ ? risc_display_output_history(api_) : nullptr;
    risc_display_rect_v1 damage{};
    bool differential = false;
    if (history) {
      copyPhysical(pixels, previous);
      if (!history->seed_previous(api_->context, target.frame)) {
        api_->release(api_->context, target.frame);
        return;
      }
      size_t minByte = info_.strideBytes, maxByte = 0, minRow = info_.height, maxRow = 0;
      for (size_t i = 0; i < rasterSize_; ++i) if (previous[i] != raster_[i]) {
        const size_t physical = flipOutput_ ? rasterSize_ - 1u - i : i;
        const size_t row = physical / info_.strideBytes, byte = physical % info_.strideBytes;
        if (byte < minByte) minByte = byte;
        if (byte > maxByte) maxByte = byte;
        if (row < minRow) minRow = row;
        if (row > maxRow) maxRow = row;
      }
      if (minRow == info_.height) {
        api_->release(api_->context, target.frame);
        lastPresentSucceeded_ = true;
        lastPresentState_ = RISC_DISPLAY_PRESENT_COMPLETE;
        return;
      }
      damage = {static_cast<int32_t>(minByte * 8u), static_cast<int32_t>(minRow),
                static_cast<uint32_t>((maxByte - minByte + 1u) * 8u),
                static_cast<uint32_t>(maxRow - minRow + 1u)};
      differential = true;
    }
    // Rotate only in the transfer copy. The renderer's reconstructed previous
    // minute and its current raster stay in the same logical coordinate space.
    copyPhysical(pixels, raster_);
    risc_display_present_options_v1 options{};
    options.intent = flipRefreshPending_ ? static_cast<uint8_t>(RISC_DISPLAY_PRESENT_CLEAN) :
        intent(nextModeRequested_ ? nextMode_ : mode);
    options.queue_policy = RISC_DISPLAY_QUEUE_FIFO;
    risc_display_present_token_v1 token = RISC_DISPLAY_PRESENT_TOKEN_INVALID;
    if (!api_->submit(api_->context, target.frame, differential ? &damage : nullptr, differential ? 1u : 0u, &options, &token) ||
        token == RISC_DISPLAY_PRESENT_TOKEN_INVALID) {
      api_->release(api_->context, target.frame);
      return;
    }
    risc_display_present_status_v1 status{};
    const bool waited = api_->wait_present(api_->context, token, timeoutMs_, &status);
    lastPresentState_ = status.state;
    lastPresentSucceeded_ = waited && status.state == RISC_DISPLAY_PRESENT_COMPLETE;
    if (lastPresentSucceeded_) {
      nextModeRequested_ = false;
      flipRefreshPending_ = false;
    } else {
      api_->release(api_->context, target.frame);
    }
  }
  void requestNextRefresh(DisplayPresentMode mode = DisplayPresentMode::Quality) override {
    nextMode_ = mode;
    nextModeRequested_ = true;
  }
  void requestNextDisplayEffect(DisplayEffect = DisplayEffect::None) override {}
  void copyGrayscaleLsbBuffers(const uint8_t*) override {}
  void copyGrayscaleMsbBuffers(const uint8_t*) override {}
  bool captureGrayscaleBaseBuffer(const uint8_t*) override { return false; }
  bool grayscaleBuffersReady() const override { return false; }
  void cleanupGrayscaleBuffers(const uint8_t*) override {}
  void displayGrayBuffer(DisplayPresentMode = DisplayPresentMode::Quality) override {
    lastPresentSucceeded_ = false;
    lastPresentState_ = RISC_DISPLAY_PRESENT_FAILED;
  }

 private:
  void copyPhysical(uint8_t* target, const uint8_t* source) const {
    for (size_t i = 0; i < rasterSize_; ++i) {
      const uint8_t pixel = static_cast<uint8_t>(~source[flipOutput_ ? rasterSize_ - 1u - i : i]);
      target[i] = flipOutput_ ? reverseBits(pixel) : pixel;
    }
  }
  static uint8_t reverseBits(uint8_t byte) {
    byte = static_cast<uint8_t>((byte >> 4) | (byte << 4));
    byte = static_cast<uint8_t>(((byte & 0xccu) >> 2) | ((byte & 0x33u) << 2));
    return static_cast<uint8_t>(((byte & 0xaau) >> 1) | ((byte & 0x55u) << 1));
  }
  static uint8_t intent(DisplayPresentMode mode) {
    switch (mode) {
      case DisplayPresentMode::Clean: return RISC_DISPLAY_PRESENT_CLEAN;
      case DisplayPresentMode::LowLatency: return RISC_DISPLAY_PRESENT_LOW_LATENCY;
      case DisplayPresentMode::Quality:
      case DisplayPresentMode::Balanced: return RISC_DISPLAY_PRESENT_QUALITY;
    }
    return RISC_DISPLAY_PRESENT_QUALITY;
  }
  const risc_display_output_api_v1* api_ = nullptr;
  uint8_t* raster_ = nullptr;
  size_t rasterSize_ = 0;
  uint32_t timeoutMs_ = 0;
  DisplaySurfaceInfo info_{};
  DisplayPresentMode nextMode_ = DisplayPresentMode::Quality;
  bool nextModeRequested_ = false;
  bool flipOutput_ = false, flipRefreshPending_ = false;
  bool ready_ = false, sleepSupported_ = false, lastPresentSucceeded_ = false;
  uint8_t lastPresentState_ = 0;
};
