#pragma once
#include <Arduino.h>
#include <Board.h>
#include <DisplaySurface.h>

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
class T5S3M5GfxDisplay;

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
namespace epd_mode {
enum epd_mode_t : uint8_t;
}
}
}  // namespace lgfx
#endif

class HalDisplay : public DisplaySurface {
 public:
  // Constructor with pin configuration
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Legacy names preserve current call sites while the renderer uses generic presentation intent.
  using RefreshMode = DisplayPresentMode;
  static constexpr RefreshMode FULL_REFRESH = RefreshMode::Clean;
  static constexpr RefreshMode HALF_REFRESH = RefreshMode::Quality;
  static constexpr RefreshMode BALANCED_REFRESH = RefreshMode::Balanced;
  static constexpr RefreshMode FAST_REFRESH = RefreshMode::LowLatency;

  // Legacy refresh ordinals are persisted in settings and mapped throughout
  // existing e-paper code. Never silently reorder these during abstraction work.
  static_assert(static_cast<uint8_t>(FULL_REFRESH) == 0u);
  static_assert(static_cast<uint8_t>(HALF_REFRESH) == 1u);
  static_assert(static_cast<uint8_t>(BALANCED_REFRESH) == 2u);
  static_assert(static_cast<uint8_t>(FAST_REFRESH) == 3u);

  using DisplayEffect = ::DisplayEffect;
  static constexpr DisplayEffect EFFECT_NONE = DisplayEffect::None;
  static constexpr DisplayEffect EFFECT_READER_TURN_FORWARD_STANDARD = DisplayEffect::PageTurnForwardStandard;
  static constexpr DisplayEffect EFFECT_READER_TURN_BACKWARD_STANDARD = DisplayEffect::PageTurnBackwardStandard;
  static constexpr DisplayEffect EFFECT_READER_TURN_FORWARD_FAST = DisplayEffect::PageTurnForwardFast;
  static constexpr DisplayEffect EFFECT_READER_TURN_BACKWARD_FAST = DisplayEffect::PageTurnBackwardFast;

  // Initialize the display hardware and driver. Ordinary boots clear the
  // panel during M5GFX initialization; deep-sleep clock timer wakes can retain
  // the physical e-paper image and skip that startup clear.
  void begin(bool clearPanel = true);

  // Exclusive native ELF display takeover. The host MUST hold RenderLock and
  // stop other display users for the entire interval. No UI/display calls are
  // allowed between successful suspend and resume. Logical framebuffers stay
  // allocated at the same addresses so the existing GfxRenderer remains valid.
  // Other boards fail closed until their backend implements a real handoff.
#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  bool suspendForExternalOwner();
  bool resumeFromExternalOwner();
#else
  bool suspendForExternalOwner() { return false; }
  bool resumeFromExternalOwner() { return false; }
#endif

  // Display dimensions
  static constexpr uint16_t VISIBLE_WIDTH = BoardPins::LogicalWidth;
  static constexpr uint16_t VISIBLE_HEIGHT = BoardPins::LogicalHeight;
  // Keep the framebuffer in physical 960x540 scan orientation while exposing
  // portrait logical coordinates to the UI.
  static constexpr uint16_t DISPLAY_WIDTH = ((BoardPins::DisplayWidth + 15) / 16) * 16;
  static constexpr uint16_t DISPLAY_HEIGHT = BoardPins::DisplayHeight;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  static_assert((DISPLAY_WIDTH % 8u) == 0u, "MONO1 scan width must be byte aligned");
  static_assert(BUFFER_SIZE == static_cast<uint32_t>(DISPLAY_WIDTH_BYTES) * DISPLAY_HEIGHT,
                "display buffer geometry must remain internally consistent");

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);
  // Compare the current logical framebuffer with a reconstructed previous frame
  // and drive only the bounding rectangle that changed. This is intended for
  // deep-sleep clients such as the desk clock, where panel contents survive but
  // RAM does not. Falls back to displayBuffer() when a full refresh is required.
  void displayBufferDiff(const uint8_t* previousBuffer, RefreshMode mode = HALF_REFRESH);
  void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);

  // When enabled, the physical panel output is mirrored 180° (whole UI upside down).
  void setFlipOutput(bool enabled);
  void requestNextRefresh(RefreshMode mode = HALF_REFRESH);
  void requestNextDisplayEffect(DisplayEffect effect = EFFECT_NONE);
  void suppressInitialFullRefresh();

  // Power management
  void deepSleep();
  // Gate panel drive power between clock updates without deinitializing SD,
  // touch or the framebuffer, as deepSleep() does.
  void setIdlePowerSaving(bool enabled);

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  bool captureGrayscaleBaseBuffer(const uint8_t* bwBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(RefreshMode mode = HALF_REFRESH);

  bool isReady() const override { return displayReady && frameBuffer != nullptr; }

  DisplaySurfaceInfo getSurfaceInfo() const override {
    return DisplaySurfaceInfo{DISPLAY_WIDTH, DISPLAY_HEIGHT, VISIBLE_WIDTH, VISIBLE_HEIGHT,
                              DISPLAY_WIDTH_BYTES, BUFFER_SIZE, DisplayPixelFormat::Mono1,
                              DisplaySafeInsets{9, 3, 9, 3}};
  }

  // Last-resort boot diagnostic that deliberately bypasses GfxRenderer and
  // runtime surface metadata. A large X plus eight code boxes gives a visible
  // indication that panel I/O still works when renderer initialization fails.
  bool showEmergencyFailurePattern(uint8_t code) {
    if (!isReady() || !frameBuffer) return false;

    memset(frameBuffer, 0xFF, BUFFER_SIZE);
    auto setPixel = [this](uint16_t x, uint16_t y, bool black) {
      if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
      uint8_t& value = frameBuffer[static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES + x / 8u];
      const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7u));
      if (black) value &= static_cast<uint8_t>(~mask);
      else value |= mask;
    };
    auto fillRect = [&](uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool black) {
      for (uint32_t yy = y; yy < static_cast<uint32_t>(y) + h && yy < DISPLAY_HEIGHT; ++yy) {
        for (uint32_t xx = x; xx < static_cast<uint32_t>(x) + w && xx < DISPLAY_WIDTH; ++xx) {
          setPixel(static_cast<uint16_t>(xx), static_cast<uint16_t>(yy), black);
        }
      }
    };

    const uint16_t border = DISPLAY_WIDTH >= 320 ? 10u : 3u;
    fillRect(0, 0, DISPLAY_WIDTH, border, true);
    fillRect(0, DISPLAY_HEIGHT - border, DISPLAY_WIDTH, border, true);
    fillRect(0, 0, border, DISPLAY_HEIGHT, true);
    fillRect(DISPLAY_WIDTH - border, 0, border, DISPLAY_HEIGHT, true);

    const uint16_t xThickness = DISPLAY_WIDTH >= 320 ? 6u : 2u;
    for (uint32_t y = border * 3u; y + border * 3u < DISPLAY_HEIGHT; ++y) {
      const uint32_t usableY = y - border * 3u;
      const uint32_t usableH = DISPLAY_HEIGHT - border * 6u;
      const uint32_t usableW = DISPLAY_WIDTH - border * 6u;
      const uint16_t x1 = static_cast<uint16_t>(border * 3u + (usableY * usableW) / usableH);
      const uint16_t x2 = static_cast<uint16_t>(DISPLAY_WIDTH - 1u - x1);
      fillRect(x1, static_cast<uint16_t>(y), xThickness, 1, true);
      fillRect(x2, static_cast<uint16_t>(y), xThickness, 1, true);
    }

    const uint16_t blockW = DISPLAY_WIDTH / 20u;
    const uint16_t blockH = DISPLAY_HEIGHT / 16u;
    const uint16_t gap = blockW / 3u;
    const uint16_t totalW = static_cast<uint16_t>(8u * blockW + 7u * gap);
    const uint16_t startX = static_cast<uint16_t>((DISPLAY_WIDTH - totalW) / 2u);
    const uint16_t startY = static_cast<uint16_t>(DISPLAY_HEIGHT - border * 2u - blockH);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint16_t x = static_cast<uint16_t>(startX + bit * (blockW + gap));
      fillRect(x, startY, blockW, blockH, true);
      if ((code & static_cast<uint8_t>(0x80u >> bit)) == 0 && blockW > 4 && blockH > 4) {
        fillRect(static_cast<uint16_t>(x + 2u), static_cast<uint16_t>(startY + 2u),
                 static_cast<uint16_t>(blockW - 4u), static_cast<uint16_t>(blockH - 4u), false);
      }
    }

    displayBuffer(FULL_REFRESH);
    return true;
  }

  // Runtime geometry passthrough
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getVisibleWidth() const;
  uint16_t getVisibleHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  T5S3M5GfxDisplay* gfx = nullptr;
  lgfx::LGFX_Sprite* panelCanvas = nullptr;
  bool externalOwner = false;
#elif defined(BOARD_LILYGO_EPD47_S3)
  uint8_t* epdFrameBuffer = nullptr;
#endif
  uint8_t* frameBuffer = nullptr;
  uint8_t* grayscaleLsbBuffer = nullptr;
  uint8_t* grayscaleMsbBuffer = nullptr;
  uint8_t* grayscaleBaseBuffer = nullptr;
  bool grayscaleBaseCaptured = false;
  bool displayReady = false;
  bool flipOutput = false;
  bool forceFullRefresh = true;
  bool forcedRefreshPending = false;
  RefreshMode forcedRefreshMode = HALF_REFRESH;
  DisplayEffect pendingDisplayEffect = EFFECT_NONE;
  uint32_t refreshCycleCount = 0;

  uint8_t* allocatePlane();
#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  void releaseBackend();
  bool initializePanelCanvas();
  void pushPanelCanvas(RefreshMode mode, lgfx::epd_mode::epd_mode_t epdMode);
  void pushPanelCanvasWithEffect(DisplayEffect effect) const;
  void renderBwToPanelCanvas() const;
  void renderBwToPanelCanvas(const uint8_t* sourceBuffer) const;
  void renderGrayToPanelCanvas() const;
#endif
};

extern HalDisplay display;
