#include "StartupScreen.h"
#include "native/NativeTouchInput.h"

#include <Arduino.h>
#include <cstring>
#include <atomic>

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
#include <T5HardwareTakeover.h>
#include <T5VideoApi.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace StartupScreen {
namespace {

bool fadePending = false;  // RenderLock protects the display handoff.
std::atomic<bool> loading{false};  // Input opens only after the first destination frame.

enum class BootBackend : uint8_t {
  None,
  Renderer,
  Video,
};

BootBackend bootBackend = BootBackend::None;

constexpr int kLogoSize = 240;
constexpr int kFrameHeight = 320;

void drawBootFrame(GfxRenderer& renderer, int rows) {
  renderer.clearScreen();

  // Geometry from docs/logo.svg, enlarged twofold for the monochrome panel.
  struct Rect {
    int x;
    int y;
    int width;
    int height;
  };

  constexpr Rect rectangles[] = {
      {10, 14, 16, 16},
      {31, 14, 16, 16},
      {52, 14, 16, 16},
      {73, 14, 16, 16},
      {94, 14, 16, 16},
      {10, 38, 47, 16},
      {63, 38, 47, 16},
      {10, 62, 100, 16},
      {10, 86, 100, 24},
  };

  constexpr int rowEnds[] = {
      5,
      7,
      8,
      9,
  };

  const int x = (renderer.getScreenWidth() - kLogoSize) / 2;
  const int y = (renderer.getScreenHeight() - kFrameHeight) / 2;

  for (int i = 0; i < rowEnds[rows - 1]; ++i) {
    const auto& rect = rectangles[i];

    renderer.fillRoundedRect(
        x + rect.x * 2,
        y + rect.y * 2,
        rect.width * 2,
        rect.height * 2,
        4,
        Color::Black);
  }

  if (rows == 4) {
    renderer.drawCenteredText(
        UI_12_FONT_ID,
        y + 250,
        "RiscRTE",
        true,
        EpdFontFamily::BOLD);

    renderer.drawCenteredText(
        SMALL_FONT_ID,
        y + 290,
        "Starting...");
  }
}

void bootWithRenderer(GfxRenderer& renderer) {
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  // Slow-panel fallback presents one useful loading frame, without spending
  // several physical refreshes playing an animation before doing any work.
  drawBootFrame(renderer, 4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  renderer.setRenderMode(mode);
  bootBackend = BootBackend::Renderer;
}

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)

constexpr char kBootVideoTag[] = "boot_video";
constexpr uint8_t kLogoLayerCount = 4;
constexpr uint32_t kRevealLayerMs = 250;
constexpr uint32_t kTextFadeMs = 300;
constexpr uint8_t kTextFadeFrames = 6;
constexpr uint32_t kRevealDeadlineMs = 1800;
constexpr uint8_t kFadeFrames = 6;
constexpr uint8_t kPulseFrames = 48;
constexpr uint8_t kFullCoverage = 64;
constexpr uint8_t kPulseMinCoverage = kFullCoverage;
constexpr uint8_t kPulseMaxCoverage = kFullCoverage;
constexpr uint32_t kReadyFadeBudgetMs = 150;
constexpr uint32_t kSubmitTimeoutMs = 350;
constexpr uint32_t kIdleTimeoutMs = 1800;

extern "C" esp_err_t native_hardware_takeover_begin(uint32_t requested);
extern "C" esp_err_t native_hardware_takeover_end(uint32_t requested);

const t5_video_api_v1* videoApi = nullptr;
t5_video_surface_v1 videoSurface{};
bool videoTakeoverActive = false;
bool videoStarted = false;
TaskHandle_t pulseTaskHandle = nullptr;
std::atomic<bool> pulseStopRequested{false};
std::atomic<bool> pulseExited{true};
// Written only by the animation worker; read after its release/acquire join.
uint8_t lastVisibleBlocks = 0;
uint8_t lastPulseCoverage = kFullCoverage;
uint8_t lastTextCoverage = 0;
uint32_t visualStartedAtMs = 0;
uint32_t visualTimeMs = 0;

constexpr uint8_t kBayer8[8][8] = {
    {0, 48, 12, 60, 3, 51, 15, 63},
    {32, 16, 44, 28, 35, 19, 47, 31},
    {8, 56, 4, 52, 11, 59, 7, 55},
    {40, 24, 36, 20, 43, 27, 39, 23},
    {2, 50, 14, 62, 1, 49, 13, 61},
    {34, 18, 46, 30, 33, 17, 45, 29},
    {10, 58, 6, 54, 9, 57, 5, 53},
    {42, 26, 38, 22, 41, 25, 37, 21},
};

struct VideoRect {
  int x;
  int y;
  int width;
  int height;
};

constexpr VideoRect kLogoRects[] = {
    {10, 14, 16, 16},
    {31, 14, 16, 16},
    {52, 14, 16, 16},
    {73, 14, 16, 16},
    {94, 14, 16, 16},
    {10, 38, 47, 16},
    {63, 38, 47, 16},
    {10, 62, 100, 16},
    {10, 86, 100, 24},
};

constexpr uint8_t kLogoLayerEnds[kLogoLayerCount] = {
    5,
    7,
    8,
    9,
};
constexpr uint8_t kLogoBlockCount = kLogoLayerEnds[kLogoLayerCount - 1];
static_assert(kLogoLayerCount * kRevealLayerMs + kTextFadeMs < kRevealDeadlineMs &&
                  kRevealDeadlineMs < 2000,
              "boot reveal must finish within two seconds");

bool elapsedAtLeast(uint32_t start, uint32_t duration) {
  return static_cast<uint32_t>(millis() - start) >= duration;
}

bool validateVideoApi(const t5_video_api_v1* api) {
  return api != nullptr && api->api_version == T5_VIDEO_API_VERSION &&
         api->struct_size >= sizeof(t5_video_api_v1) && api->start != nullptr &&
         api->backbuffer != nullptr && api->can_submit != nullptr &&
         api->submit != nullptr && api->pending != nullptr &&
         api->frame_counter != nullptr && api->stop != nullptr;
}

bool validateVideoSurface(const t5_video_surface_v1& surface) {
  if (surface.pixel_format != T5_VIDEO_PIXEL_MONO_1BPP_MSB) {
    return false;
  }
  if (surface.width < static_cast<uint16_t>(kFrameHeight) ||
      surface.height < static_cast<uint16_t>(kLogoSize)) {
    return false;
  }
  return surface.stride_bytes >= static_cast<uint16_t>((surface.width + 7U) / 8U);
}

uint8_t whiteByte() {
  return (videoSurface.flags & T5_VIDEO_FLAG_ONE_IS_BLACK) != 0U ? 0x00U : 0xFFU;
}

void setPhysicalPixel(uint8_t* buffer, size_t bufferSize, int logicalX,
                      int logicalY, bool black) {
  if (buffer == nullptr || logicalX < 0 || logicalY < 0 ||
      logicalX >= static_cast<int>(videoSurface.height) ||
      logicalY >= static_cast<int>(videoSurface.width)) {
    return;
  }

  // The fast-video surface is physical landscape. RiscRTE's boot artwork is
  // authored in portrait coordinates: logical (x,y) -> panel (y, H-1-x).
  const int panelX = logicalY;
  const int panelY = static_cast<int>(videoSurface.height) - 1 - logicalX;
  const size_t offset = static_cast<size_t>(panelY) * videoSurface.stride_bytes +
                        static_cast<size_t>(panelX >> 3);
  if (offset >= bufferSize) {
    return;
  }

  const uint8_t mask = static_cast<uint8_t>(0x80U >> (panelX & 7));
  const bool oneIsBlack =
      (videoSurface.flags & T5_VIDEO_FLAG_ONE_IS_BLACK) != 0U;
  const bool setBit = black == oneIsBlack;
  if (setBit) {
    buffer[offset] |= mask;
  } else {
    buffer[offset] &= static_cast<uint8_t>(~mask);
  }
}

bool ditherPixel(int x, int y, uint8_t coverage) {
  return coverage >= kFullCoverage || kBayer8[y & 7][x & 7] < coverage;
}

bool insideRoundedRect(int px, int py, int width, int height, int radius) {
  if (radius <= 0 || (px >= radius && px < width - radius) ||
      (py >= radius && py < height - radius)) {
    return true;
  }

  const int centerX = px < radius ? radius - 1 : width - radius;
  const int centerY = py < radius ? radius - 1 : height - radius;
  const int dx = px - centerX;
  const int dy = py - centerY;
  return dx * dx + dy * dy <= radius * radius;
}

void drawDitheredRoundedRect(uint8_t* buffer, size_t bufferSize, int x, int y,
                             int width, int height, int radius,
                             uint8_t coverage) {
  for (int py = 0; py < height; ++py) {
    for (int px = 0; px < width; ++px) {
      const int screenX = x + px;
      const int screenY = y + py;
      if (insideRoundedRect(px, py, width, height, radius) &&
          ditherPixel(screenX, screenY, coverage)) {
        setPhysicalPixel(buffer, bufferSize, screenX, screenY, true);
      }
    }
  }
}

const uint8_t* glyphFor(char ch) {
  static constexpr uint8_t blank[5] = {0, 0, 0, 0, 0};
  static constexpr uint8_t letters[26][5] = {
      {0x7E, 0x09, 0x09, 0x09, 0x7E},
      {0x7F, 0x49, 0x49, 0x49, 0x36},
      {0x3E, 0x41, 0x41, 0x41, 0x22},
      {0x7F, 0x41, 0x41, 0x22, 0x1C},
      {0x7F, 0x49, 0x49, 0x49, 0x41},
      {0x7F, 0x09, 0x09, 0x09, 0x01},
      {0x3E, 0x41, 0x49, 0x49, 0x7A},
      {0x7F, 0x08, 0x08, 0x08, 0x7F},
      {0x00, 0x41, 0x7F, 0x41, 0x00},
      {0x20, 0x40, 0x41, 0x3F, 0x01},
      {0x7F, 0x08, 0x14, 0x22, 0x41},
      {0x7F, 0x40, 0x40, 0x40, 0x40},
      {0x7F, 0x02, 0x0C, 0x02, 0x7F},
      {0x7F, 0x04, 0x08, 0x10, 0x7F},
      {0x3E, 0x41, 0x41, 0x41, 0x3E},
      {0x7F, 0x09, 0x09, 0x09, 0x06},
      {0x3E, 0x41, 0x51, 0x21, 0x5E},
      {0x7F, 0x09, 0x19, 0x29, 0x46},
      {0x26, 0x49, 0x49, 0x49, 0x32},
      {0x01, 0x01, 0x7F, 0x01, 0x01},
      {0x3F, 0x40, 0x40, 0x40, 0x3F},
      {0x1F, 0x20, 0x40, 0x20, 0x1F},
      {0x7F, 0x20, 0x18, 0x20, 0x7F},
      {0x63, 0x14, 0x08, 0x14, 0x63},
      {0x03, 0x04, 0x78, 0x04, 0x03},
      {0x61, 0x51, 0x49, 0x45, 0x43},
  };
  static constexpr uint8_t dot[5] = {0, 0x60, 0x60, 0, 0};

  if (ch >= 'a' && ch <= 'z') {
    ch = static_cast<char>(ch - ('a' - 'A'));
  }
  if (ch >= 'A' && ch <= 'Z') {
    return letters[ch - 'A'];
  }
  if (ch == '.') {
    return dot;
  }
  return blank;
}

void drawDitheredText(uint8_t* buffer, size_t bufferSize, int x, int y,
                      const char* text, int scale, uint8_t coverage) {
  if (text == nullptr || scale <= 0) {
    return;
  }

  for (const char* p = text; *p != '\0'; ++p) {
    const uint8_t* glyph = glyphFor(*p);
    for (int column = 0; column < 5; ++column) {
      for (int row = 0; row < 7; ++row) {
        if ((glyph[column] & (1U << row)) == 0U) {
          continue;
        }
        for (int sy = 0; sy < scale; ++sy) {
          for (int sx = 0; sx < scale; ++sx) {
            const int screenX = x + column * scale + sx;
            const int screenY = y + row * scale + sy;
            if (ditherPixel(screenX, screenY, coverage)) {
              setPhysicalPixel(buffer, bufferSize, screenX, screenY, true);
            }
          }
        }
      }
    }
    x += 6 * scale;
  }
}

int textWidth(const char* text, int scale) {
  if (text == nullptr || text[0] == '\0') {
    return 0;
  }
  return static_cast<int>((std::strlen(text) * 6U - 1U) *
                          static_cast<size_t>(scale));
}

// A folded plate opens around its central hinge. Row spans keep raster work
// bounded by the final box area; no particles, textures or temporary buffers.
void drawAssemblingBlock(uint8_t* buffer, size_t size, int x, int y,
                         int width, int height, uint32_t age, uint8_t coverage) {
  constexpr uint32_t settleMs = 200;
  if (age >= settleMs) {
    drawDitheredRoundedRect(buffer, size, x, y, width, height, 4, coverage);
    return;
  }
  // Cubic ease-out: arrive quickly, then gently flatten into the logo plane.
  const int remaining = static_cast<int>(settleMs - age);
  const int fold = remaining * remaining * remaining / 8000; // 1000 -> 0
  const int plateWidth = width * (1000 - fold / 2) / 1000;
  const int plateHeight = height * (1000 - fold / 2) / 1000;
  const int left = x + (width - plateWidth) / 2;
  const int top = y + (height - plateHeight) / 2 - fold * 18 / 1000;
  const int tilt = fold * 12 / 1000;
  const int seam = fold > 160 ? 1 : 0;
  for (int row = 0; row < plateHeight; ++row) {
    const int skew = tilt * (plateHeight - 1 - 2 * row) / plateHeight;
    for (int column = 0; column < plateWidth; ++column) {
      // Two clean facets close around a white hinge, disappearing at rest.
      if (seam && column >= plateWidth / 2 - seam &&
          column < plateWidth / 2 + seam) continue;
      if (ditherPixel(left + column + skew, top + row, coverage))
        setPhysicalPixel(buffer, size, left + column + skew, top + row, true);
    }
  }
}

void drawVideoLogo(uint8_t* buffer, size_t bufferSize, uint8_t visibleBlocks,
                   uint8_t logoCoverage, uint8_t textCoverage) {
  // Fixed logo positions; each block assembles in left-to-right array order.
  if (visibleBlocks > kLogoBlockCount) visibleBlocks = kLogoBlockCount;

  const int logicalWidth = static_cast<int>(videoSurface.height);
  const int logicalHeight = static_cast<int>(videoSurface.width);
  const int frameX = (logicalWidth - kLogoSize) / 2;
  const int frameY = (logicalHeight - kFrameHeight) / 2;


  for (uint8_t rectIndex = 0; rectIndex < visibleBlocks; ++rectIndex) {
    const auto& rect = kLogoRects[rectIndex];
    uint8_t layer=0, first=0;
    while (rectIndex>=kLogoLayerEnds[layer]) { first=kLogoLayerEnds[layer]; ++layer; }
    const uint32_t born=layer*kRevealLayerMs+(rectIndex-first+1u)*kRevealLayerMs/(kLogoLayerEnds[layer]-first);
    const uint32_t age=visualTimeMs>born ? visualTimeMs-born : 0;
    drawAssemblingBlock(buffer, bufferSize, frameX + rect.x * 2,
                         frameY + rect.y * 2, rect.width * 2,
                         rect.height * 2, age, logoCoverage);
  }

  // Both labels stay at fixed coordinates and first appear after all blocks.
  if (visibleBlocks == kLogoBlockCount && textCoverage != 0U) {
    constexpr char title[] = "RISCRTE";
    constexpr char status[] = "STARTING...";
    constexpr int titleScale = 4;
    constexpr int statusScale = 2;

    drawDitheredText(buffer, bufferSize,
                     (logicalWidth - textWidth(title, titleScale)) / 2,
                     frameY + 244, title, titleScale, textCoverage);
    drawDitheredText(buffer, bufferSize,
                     (logicalWidth - textWidth(status, statusScale)) / 2,
                     frameY + 291, status, statusScale, textCoverage);

  }
}

bool waitUntilVideoCanSubmit(uint32_t timeoutMs, bool cancellable) {
  const uint32_t start = millis();
  while (videoApi != nullptr && !videoApi->can_submit()) {
    if ((cancellable && pulseStopRequested.load(std::memory_order_relaxed)) ||
        elapsedAtLeast(start, timeoutMs)) {
      return false;
    }
    delay(1);
  }
  return videoApi != nullptr &&
      !(cancellable && pulseStopRequested.load(std::memory_order_relaxed));
}

bool submitVideoFrame(uint8_t visibleBlocks, uint8_t logoCoverage,
                      uint8_t textCoverage,
                      uint32_t submitTimeoutMs = kSubmitTimeoutMs, bool cancellable = true) {
  if (!videoStarted || videoApi == nullptr ||
      !waitUntilVideoCanSubmit(submitTimeoutMs, cancellable)) {
    return false;
  }

  size_t bufferSize = 0;
  uint8_t* buffer = videoApi->backbuffer(&bufferSize);
  const size_t requiredSize =
      static_cast<size_t>(videoSurface.stride_bytes) * videoSurface.height;
  if (buffer == nullptr || bufferSize < requiredSize) {
    return false;
  }

  if (cancellable) visualTimeMs=static_cast<uint32_t>(millis()-visualStartedAtMs);
  std::memset(buffer, whiteByte(), bufferSize);
  drawVideoLogo(buffer, bufferSize, visibleBlocks, logoCoverage, textCoverage);
  return videoApi->submit(0, 0);
}

void waitForVideoIdle(uint32_t timeoutMs) {
  if (videoApi == nullptr) {
    return;
  }
  const uint32_t start = millis();
  while (videoApi->pending() && !elapsedAtLeast(start, timeoutMs)) {
    delay(1);
  }
}

bool releaseVideoOwner() {
  if (videoStarted && videoApi != nullptr) videoApi->stop();
  if (videoTakeoverActive) {
    const esp_err_t rc = native_hardware_takeover_end(
        T5_HARDWARE_TAKEOVER_DISPLAY | T5_HARDWARE_TAKEOVER_UI_VIDEO);
    if (rc != ESP_OK) {
      ESP_LOGE(kBootVideoTag, "display takeover release failed: %s", esp_err_to_name(rc));
      return false;  // Keep ownership/state for a safe retry; no overlapping owner.
    }
  }
  videoStarted = false;
  videoApi = nullptr;
  videoSurface = {};
  videoTakeoverActive = false;
  return true;
}

uint8_t smoothCoverage(uint8_t frame, uint8_t frameCount,
                       uint8_t startCoverage, uint8_t endCoverage) {
  const uint32_t t =
      static_cast<uint32_t>(frame) * 1024U / static_cast<uint32_t>(frameCount);
  const uint32_t smooth =
      static_cast<uint32_t>((static_cast<uint64_t>(t) * t *
                             (3072U - 2U * t)) /
                            (1024ULL * 1024ULL));
  return static_cast<uint8_t>(
      startCoverage +
      (static_cast<uint32_t>(endCoverage - startCoverage) * smooth) / 1024U);
}

bool renderLayerReveal();

void pulseTask(void*) {
  // Reveal and pulse both run alongside startup. Neither holds RenderLock,
  // accesses the renderer, scans SD, nor loads provider modules.
  const bool revealed = renderLayerReveal();
  uint8_t phase = static_cast<uint8_t>(kPulseFrames / 2U - 1U);
  while (revealed && !pulseStopRequested.load(std::memory_order_relaxed)) {
    const uint8_t ramp = phase < (kPulseFrames / 2U)
                             ? phase
                             : static_cast<uint8_t>(kPulseFrames - 1U - phase);
    const uint8_t coverage =
        smoothCoverage(ramp, static_cast<uint8_t>(kPulseFrames / 2U - 1U),
                       kPulseMinCoverage, kPulseMaxCoverage);
    if (submitVideoFrame(kLogoBlockCount, coverage, kFullCoverage)) {
      lastVisibleBlocks = kLogoBlockCount;
      lastPulseCoverage = coverage;
      lastTextCoverage = kFullCoverage;
      phase = static_cast<uint8_t>((phase + 1U) % kPulseFrames);
    }
    delay(35);  // Cap cosmetic work so real startup keeps the CPU.
  }
  pulseExited.store(true, std::memory_order_release);
  vTaskDelete(nullptr);  // No video/shared-state access after publishing exit.
}

bool startPulseTask() {
  pulseStopRequested.store(false, std::memory_order_relaxed);
  pulseExited.store(false, std::memory_order_relaxed);
  visualStartedAtMs = millis();
  visualTimeMs = 0;
  lastVisibleBlocks = 0;
  lastPulseCoverage = kFullCoverage;
  lastTextCoverage = 0;
  const BaseType_t rc = xTaskCreatePinnedToCore(
      pulseTask, "boot_animation", 4096, nullptr, 1, &pulseTaskHandle, 0);
  if (rc != pdPASS) {
    pulseTaskHandle = nullptr;
    pulseExited.store(true, std::memory_order_release);
  }
  return rc == pdPASS;
}

bool stopPulseTask() {
  pulseStopRequested.store(true, std::memory_order_relaxed);
  const uint32_t start = millis();
  while (!pulseExited.load(std::memory_order_acquire) && !elapsedAtLeast(start, 1000U)) delay(1);
  if (!pulseExited.load(std::memory_order_acquire)) {
    ESP_LOGW(kBootVideoTag, "animation still stopping; retaining video owner for retry");
    return false;  // Never delete a task inside driver code or free its buffers.
  }
  pulseTaskHandle = nullptr;
  return true;
}

bool waitForRevealTime(uint32_t start, uint32_t due) {
  while (!elapsedAtLeast(start, due)) {
    if (pulseStopRequested.load(std::memory_order_relaxed)) return false;
    delay(1);
  }
  return !pulseStopRequested.load(std::memory_order_relaxed);
}

bool renderLayerReveal() {
  // Preserve the left-to-right block build; animate the light field between
  // block arrivals rather than pausing the image at each discrete layer.
  const uint32_t start=millis();
  constexpr uint32_t duration=kLogoLayerCount*kRevealLayerMs+kTextFadeMs;
  uint32_t elapsed=0;
  do {
    if (pulseStopRequested.load(std::memory_order_relaxed)) return false;
    elapsed=static_cast<uint32_t>(millis()-start);
    if (elapsed>=kRevealDeadlineMs) return false;
    uint8_t visible=0, firstBlock=0;
    for (uint8_t layer=0; layer<kLogoLayerCount; ++layer) {
      const uint8_t lastBlock=kLogoLayerEnds[layer];
      const uint8_t blocksInLayer=lastBlock-firstBlock;
      for (uint8_t block=firstBlock+1; block<=lastBlock; ++block) {
        const uint32_t due=layer*kRevealLayerMs+(block-firstBlock)*kRevealLayerMs/blocksInLayer;
        if (elapsed>=due) visible=block;
      }
      firstBlock=lastBlock;
    }
    const uint32_t textElapsed=elapsed>kLogoLayerCount*kRevealLayerMs ? elapsed-kLogoLayerCount*kRevealLayerMs : 0;
    const uint8_t textFrame=static_cast<uint8_t>(textElapsed>=kTextFadeMs ? kTextFadeFrames : textElapsed*kTextFadeFrames/kTextFadeMs);
    const uint8_t textCoverage=smoothCoverage(textFrame,kTextFadeFrames,0,kFullCoverage);
    const uint32_t left=kRevealDeadlineMs-elapsed;
    if (!submitVideoFrame(visible,kFullCoverage,textCoverage,left<kSubmitTimeoutMs?left:kSubmitTimeoutMs)) return false;
    lastVisibleBlocks=visible;
    lastPulseCoverage=kFullCoverage;
    lastTextCoverage=textCoverage;
    if (elapsed<duration && !waitForRevealTime(start,elapsed+40u)) return false;
  } while (elapsed<duration);
  return true;
}

bool bootWithVideo(GfxRenderer& renderer) {
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  renderer.setRenderMode(mode);

  const esp_err_t takeoverRc =
      native_hardware_takeover_begin(T5_HARDWARE_TAKEOVER_DISPLAY | T5_HARDWARE_TAKEOVER_UI_VIDEO);
  if (takeoverRc != ESP_OK) {
    ESP_LOGW(kBootVideoTag, "display takeover unavailable: %s",
             esp_err_to_name(takeoverRc));
    return false;
  }
  videoTakeoverActive = true;
  bootBackend = BootBackend::Video;

  videoApi = t5_video_get_api(T5_VIDEO_API_VERSION);
  if (!validateVideoApi(videoApi) || !videoApi->start(&videoSurface)) {
    ESP_LOGE(kBootVideoTag, "EPD video service did not start");
    return !releaseVideoOwner();
  }
  videoStarted = true;

  if (!validateVideoSurface(videoSurface)) {
    ESP_LOGE(kBootVideoTag,
             "unsupported video surface %ux%u stride=%u format=%u",
             videoSurface.width, videoSurface.height,
             videoSurface.stride_bytes, videoSurface.pixel_format);
    return !releaseVideoOwner();
  }

  if (!startPulseTask()) {
    ESP_LOGE(kBootVideoTag, "boot animation worker unavailable");
    return !releaseVideoOwner();
  }

  bootBackend = BootBackend::Video;
  return true;
}

bool finishVideoBoot(GfxRenderer& renderer) {
  if (!stopPulseTask()) return false;
  // Readiness cancels even a partly drawn reveal. Fade only what was actually
  // submitted, with a total deadline; never finish the sequence just for show.
  const uint32_t start = millis();
  if (videoStarted && lastVisibleBlocks) {
    for (uint8_t frame = 1; frame <= kFadeFrames; ++frame) {
      const uint32_t elapsed = static_cast<uint32_t>(millis() - start);
      if (elapsed >= kReadyFadeBudgetMs) break;
      const uint32_t remaining = kFadeFrames - frame;
      const uint8_t coverage = lastPulseCoverage * remaining * remaining / (kFadeFrames * kFadeFrames);
      const uint8_t textCoverage = lastTextCoverage * remaining * remaining / (kFadeFrames * kFadeFrames);
      if (!submitVideoFrame(lastVisibleBlocks, coverage, textCoverage, kReadyFadeBudgetMs - elapsed, false)) break;
    }
  }
  if (videoStarted) waitForVideoIdle(kIdleTimeoutMs);
  if (!releaseVideoOwner()) return false;
  renderer.requestNextRefresh(HalDisplay::FULL_REFRESH);
  return true;
}

#endif

}  // namespace

void boot(GfxRenderer& renderer) {
  loading.store(true, std::memory_order_release);
  fadePending = false;
  bootBackend = BootBackend::None;

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  if (bootWithVideo(renderer)) {
    fadePending = true;
    return;
  }
#endif

  bootWithRenderer(renderer);
  fadePending = true;
}

void armBootFade() {
  loading.store(true, std::memory_order_release);
  fadePending = true;
}

bool isLoading() { return loading.load(std::memory_order_acquire); }

void destinationReady() {
  if (!isLoading() || fadePending) return;
  nativeTouchDiscardGestures();
  loading.store(false, std::memory_order_release);
}

bool finishBoot(GfxRenderer& renderer) {
  if (!fadePending) return true;

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  if (bootBackend == BootBackend::Video) {
    if (!finishVideoBoot(renderer)) return false;
    bootBackend = BootBackend::None;
    fadePending = false;
    return true;
  }
#endif

  if (bootBackend == BootBackend::Renderer) {
    renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  }
  bootBackend = BootBackend::None;
  fadePending = false;
  return true;
}

}  // namespace StartupScreen
