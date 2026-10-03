#include <HalDisplay.h>

#if defined(BOARD_XTEINK_X4_PRO)

#include <Logging.h>

HalDisplay display;

HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;

bool HalDisplay::attachProvider(DisplaySurface& surface) {
  displayReady = false;
  providerSurface = nullptr;
  const auto info = surface.getSurfaceInfo();
  if (!surface.isReady() || info.width != DISPLAY_WIDTH || info.height != DISPLAY_HEIGHT ||
      info.visibleWidth != VISIBLE_WIDTH || info.visibleHeight != VISIBLE_HEIGHT ||
      info.strideBytes != DISPLAY_WIDTH_BYTES || info.bufferSize != BUFFER_SIZE ||
      info.pixelFormat != DisplayPixelFormat::Mono1 ||
      !surface.getFrameBuffer()) {
    LOG_ERR("DSP", "X4 provider surface geometry rejected");
    return false;
  }
  providerSurface = &surface;
  return true;
}

void HalDisplay::begin(bool) {
  displayReady = providerSurface && providerSurface->isReady() && providerSurface->getFrameBuffer();
  if (!displayReady) LOG_ERR("DSP", "X4 display.output provider unavailable");
}
void HalDisplay::clearScreen(uint8_t color) const {
  if (isReady()) providerSurface->clearScreen(color);
}
void HalDisplay::drawImage(const uint8_t* image, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h, bool fromProgmem) const {
  if (isReady()) providerSurface->drawImage(image, x, y, w, h, fromProgmem);
}
void HalDisplay::drawImageTransparent(const uint8_t* image, uint16_t x, uint16_t y,
                                      uint16_t w, uint16_t h, bool fromProgmem) const {
  if (isReady()) providerSurface->drawImageTransparent(image, x, y, w, h, fromProgmem);
}
void HalDisplay::displayBuffer(RefreshMode mode, bool) {
  if (isReady()) providerSurface->displayBuffer(mode);
}
void HalDisplay::displayBufferDiff(const uint8_t*, RefreshMode mode) { displayBuffer(mode); }
void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }
void HalDisplay::setFlipOutput(bool) {}
void HalDisplay::requestNextRefresh(RefreshMode mode) {
  if (isReady()) providerSurface->requestNextRefresh(mode);
}
void HalDisplay::requestNextDisplayEffect(DisplayEffect effect) {
  if (isReady()) providerSurface->requestNextDisplayEffect(effect);
}
void HalDisplay::suppressInitialFullRefresh() {}
bool HalDisplay::deepSleep() { return false; }
void HalDisplay::setIdlePowerSaving(bool) {}
uint8_t* HalDisplay::getFrameBuffer() const {
  return providerSurface ? providerSurface->getFrameBuffer() : nullptr;
}
void HalDisplay::copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* buffer) {
  if (isReady()) providerSurface->copyGrayscaleLsbBuffers(buffer);
}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* buffer) {
  if (isReady()) providerSurface->copyGrayscaleMsbBuffers(buffer);
}
bool HalDisplay::captureGrayscaleBaseBuffer(const uint8_t* buffer) {
  return isReady() && providerSurface->captureGrayscaleBaseBuffer(buffer);
}
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* buffer) {
  if (isReady()) providerSurface->cleanupGrayscaleBuffers(buffer);
}
void HalDisplay::displayGrayBuffer(RefreshMode mode) {
  if (isReady()) providerSurface->displayGrayBuffer(mode);
}
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getVisibleWidth() const { return VISIBLE_WIDTH; }
uint16_t HalDisplay::getVisibleHeight() const { return VISIBLE_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

#endif
