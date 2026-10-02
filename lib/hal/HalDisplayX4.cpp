#include <HalDisplay.h>

#if defined(BOARD_XTEINK_X4_PRO)

#include "x4pro_mmio.h"
#include "x4pro_pins.h"

#include <Logging.h>
#include <esp_heap_caps.h>
#include <cstring>

HalDisplay display;

namespace {
bool waitIdle() {
  for (uint32_t i = 0; i < 2000; ++i) {
    if (!x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) return true;
    delay(1);
  }
  return false;
}
void spiByte(uint8_t value) {
  for (int bit = 7; bit >= 0; --bit) {
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, (value >> bit) & 1);
    x4pro_pin_output(X4PRO_PIN_EPD_SCLK, true);
    x4pro_pin_output(X4PRO_PIN_EPD_SCLK, false);
  }
}
void command(uint8_t cmd) {
  x4pro_pin_output(X4PRO_PIN_EPD_DC, false);
  x4pro_pin_output(X4PRO_PIN_EPD_CS, false);
  spiByte(cmd);
  x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
}
void data1(uint8_t value) {
  x4pro_pin_output(X4PRO_PIN_EPD_DC, true);
  x4pro_pin_output(X4PRO_PIN_EPD_CS, false);
  spiByte(value);
  x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
}
bool initPanel() {
  static const uint8_t booster[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
  static const uint8_t gate[] = {0xDF, 0x01, 0x02};
  static const uint8_t xWindow[] = {0x00, 0x00, 0x1F, 0x03};
  static const uint8_t yWindow[] = {0x00, 0x00, 0xDF, 0x01};
  x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
  x4pro_pin_output(X4PRO_PIN_EPD_SCLK, false);
  x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
  delay(10);
  x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
  delay(20);
  if (!waitIdle()) return false;
  command(0x01);
  for (uint8_t b : gate) data1(b);
  command(0x03);
  data1(0x17);
  command(0x04);
  for (uint8_t b : booster) data1(b);
  command(0x11);
  data1(0x03);
  command(0x44);
  for (uint8_t b : xWindow) data1(b);
  command(0x45);
  for (uint8_t b : yWindow) data1(b);
  command(0x3C);
  data1(0xC0);
  return waitIdle();
}
void present(const uint8_t* buffer, size_t length) {
  command(0x4E); data1(0x00); data1(0x00);
  command(0x4F); data1(0x00); data1(0x00);
  command(0x24);
  x4pro_pin_output(X4PRO_PIN_EPD_DC, true);
  x4pro_pin_output(X4PRO_PIN_EPD_CS, false);
  for (size_t i = 0; i < length; ++i) spiByte(buffer[i]);
  x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
  command(0x22); data1(0xF7); command(0x20);
  (void)waitIdle();
}
}

HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() { free(frameBuffer); }

void HalDisplay::begin(const bool clearPanel) {
  if (!frameBuffer) {
    frameBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!frameBuffer) frameBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (!frameBuffer) return;
  memset(frameBuffer, 0xFF, BUFFER_SIZE);
  displayReady = initPanel();
  if (displayReady && clearPanel) present(frameBuffer, BUFFER_SIZE);
  LOG_INF("DSP", "X4 Pro panel %s", displayReady ? "ready" : "failed");
}
void HalDisplay::clearScreen(uint8_t color) const {
  if (frameBuffer) memset(frameBuffer, color, BUFFER_SIZE);
}
void HalDisplay::drawImage(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool) const {}
void HalDisplay::drawImageTransparent(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool) const {}
void HalDisplay::displayBuffer(RefreshMode, bool) { if (displayReady && frameBuffer) present(frameBuffer, BUFFER_SIZE); }
void HalDisplay::displayBufferDiff(const uint8_t*, RefreshMode) { displayBuffer(); }
void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }
void HalDisplay::setFlipOutput(bool) {}
void HalDisplay::requestNextRefresh(RefreshMode) {}
void HalDisplay::requestNextDisplayEffect(DisplayEffect) {}
void HalDisplay::suppressInitialFullRefresh() {}
void HalDisplay::deepSleep() {}
void HalDisplay::setIdlePowerSaving(bool) {}
uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }
void HalDisplay::copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
bool HalDisplay::captureGrayscaleBaseBuffer(const uint8_t*) { return false; }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(RefreshMode) { displayBuffer(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getVisibleWidth() const { return VISIBLE_WIDTH; }
uint16_t HalDisplay::getVisibleHeight() const { return VISIBLE_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

#endif
