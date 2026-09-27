#include "StartupScreen.h"

#include <Arduino.h>
#include <cstring>

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

bool fadePending = false;  // Accessed only while holding RenderLock.

enum class BootBackend : uint8_t {
  None,
  Renderer,
  Video,
};

BootBackend bootBackend = BootBackend::None;

constexpr unsigned long kRevealBudgetMs = 3500;

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

  // Initial panel refresh is intentionally separate from the reveal timer.
  drawBootFrame(renderer, 1);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  delay(1);

  const unsigned long revealStart = millis();

  for (int rows = 2; rows <= 4; ++rows) {
    if (rows < 4 && millis() - revealStart >= kRevealBudgetMs) {
      rows = 4;
    }

    drawBootFrame(renderer, rows);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(1);
  }

  renderer.setRenderMode(mode);
  bootBackend = BootBackend::Renderer;
}

void finishRendererBoot(GfxRenderer& renderer) {
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);

  constexpr uint8_t bayer[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };

  const int x = (renderer.getScreenWidth() - kLogoSize) / 2;
  const int y = (renderer.getScreenHeight() - kFrameHeight) / 2;

  constexpr int fadeThresholds[] = {
      4,
      8,
      12,
  };

  for (const int threshold : fadeThresholds) {
    drawBootFrame(renderer, 4);

    for (int py = 0; py < kFrameHeight; ++py) {
      for (int px = 0; px < kLogoSize; ++px) {
        if (bayer[py & 3][px & 3] < threshold) {
          renderer.drawPixel(x + px, y + py, false);
        }
      }

      if ((py & 63) == 63) {
        delay(1);
      }
    }

    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  renderer.setRenderMode(mode);
  renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
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
constexpr uint8_t kPulseMinCoverage = 24;
constexpr uint8_t kPulseMaxCoverage = kFullCoverage;
constexpr uint32_t kMinimumPulseMs = 600;
constexpr uint32_t kSubmitTimeoutMs = 350;
constexpr uint32_t kIdleTimeoutMs = 1800;

extern "C" esp_err_t native_hardware_takeover_begin(uint32_t requested);
extern "C" esp_err_t native_hardware_takeover_end(uint32_t requested);

const t5_video_api_v1* videoApi = nullptr;
t5_video_surface_v1 videoSurface{};
bool videoTakeoverActive = false;
bool videoStarted = false;
TaskHandle_t pulseTaskHandle = nullptr;
volatile bool pulseStopRequested = false;
volatile uint8_t lastPulseCoverage = kFullCoverage;
uint32_t pulseStartedAtMs = 0;

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

void drawVideoLogo(uint8_t* buffer, size_t bufferSize, uint8_t visibleBlocks,
                   uint8_t logoCoverage, uint8_t textCoverage) {
  // Fixed artwork: each block appears whole, in left-to-right array order.
  if (visibleBlocks > kLogoBlockCount) visibleBlocks = kLogoBlockCount;

  const int logicalWidth = static_cast<int>(videoSurface.height);
  const int logicalHeight = static_cast<int>(videoSurface.width);
  const int frameX = (logicalWidth - kLogoSize) / 2;
  const int frameY = (logicalHeight - kFrameHeight) / 2;

  for (uint8_t rectIndex = 0; rectIndex < visibleBlocks; ++rectIndex) {
    const auto& rect = kLogoRects[rectIndex];
    drawDitheredRoundedRect(buffer, bufferSize, frameX + rect.x * 2,
                            frameY + rect.y * 2, rect.width * 2,
                            rect.height * 2, 4, logoCoverage);
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

bool waitUntilVideoCanSubmit(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (videoApi != nullptr && !videoApi->can_submit()) {
    if (elapsedAtLeast(start, timeoutMs)) {
      return false;
    }
    delay(1);
  }
  return videoApi != nullptr;
}

bool submitVideoFrame(uint8_t visibleBlocks, uint8_t logoCoverage,
                      uint8_t textCoverage,
                      uint32_t submitTimeoutMs = kSubmitTimeoutMs) {
  if (!videoStarted || videoApi == nullptr ||
      !waitUntilVideoCanSubmit(submitTimeoutMs)) {
    return false;
  }

  size_t bufferSize = 0;
  uint8_t* buffer = videoApi->backbuffer(&bufferSize);
  const size_t requiredSize =
      static_cast<size_t>(videoSurface.stride_bytes) * videoSurface.height;
  if (buffer == nullptr || bufferSize < requiredSize) {
    return false;
  }

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

void releaseVideoOwner() {
  pulseStopRequested = true;

  if (videoStarted && videoApi != nullptr) {
    videoApi->stop();
  }
  videoStarted = false;
  videoApi = nullptr;
  videoSurface = {};

  if (videoTakeoverActive) {
    const esp_err_t rc =
        native_hardware_takeover_end(T5_HARDWARE_TAKEOVER_DISPLAY);
    if (rc != ESP_OK) {
      ESP_LOGE(kBootVideoTag, "display takeover release failed: %s",
               esp_err_to_name(rc));
    }
  }
  videoTakeoverActive = false;
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

void pulseTask(void*) {
  // Begin at full density so the first loading frame does not jump lighter
  // immediately after the fourth layer completes.
  uint8_t phase = static_cast<uint8_t>(kPulseFrames / 2U - 1U);

  while (!pulseStopRequested) {
    const uint8_t ramp = phase < (kPulseFrames / 2U)
                             ? phase
                             : static_cast<uint8_t>(kPulseFrames - 1U - phase);
    const uint8_t coverage =
        smoothCoverage(ramp, static_cast<uint8_t>(kPulseFrames / 2U - 1U),
                       kPulseMinCoverage, kPulseMaxCoverage);

    if (submitVideoFrame(kLogoBlockCount, coverage, kFullCoverage)) {
      lastPulseCoverage = coverage;
      phase = static_cast<uint8_t>((phase + 1U) % kPulseFrames);
    } else {
      delay(2);
    }
  }

  pulseTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

bool startPulseTask() {
  pulseStopRequested = false;
  lastPulseCoverage = kFullCoverage;
  pulseStartedAtMs = millis();
  const BaseType_t rc = xTaskCreatePinnedToCore(
      pulseTask, "boot_pulse", 4096, nullptr, 1, &pulseTaskHandle, 0);
  return rc == pdPASS;
}

void stopPulseTask() {
  if (pulseTaskHandle == nullptr) {
    return;
  }

  pulseStopRequested = true;
  const uint32_t start = millis();
  while (pulseTaskHandle != nullptr && !elapsedAtLeast(start, 1000U)) {
    delay(1);
  }

  if (pulseTaskHandle != nullptr) {
    TaskHandle_t stuckTask = pulseTaskHandle;
    pulseTaskHandle = nullptr;
    vTaskDelete(stuckTask);
    ESP_LOGW(kBootVideoTag, "forced stalled pulse task to stop");
  }
}

bool renderLayerReveal() {
  // Each row gets the same 250 ms. Five top blocks pop every 50 ms, the next
  // two every 125 ms, and each of the two wide blocks uses a whole row interval.
  const uint32_t start = millis();
  const auto submitBeforeDeadline = [start](uint8_t visibleBlocks,
                                            uint8_t textCoverage) {
    const uint32_t elapsed = static_cast<uint32_t>(millis() - start);
    if (elapsed >= kRevealDeadlineMs) return false;
    const uint32_t left = kRevealDeadlineMs - elapsed;
    const uint32_t timeout = left < kSubmitTimeoutMs ? left : kSubmitTimeoutMs;
    return submitVideoFrame(visibleBlocks, kFullCoverage, textCoverage, timeout);
  };
  uint8_t firstBlock = 0;
  for (uint8_t layer = 0; layer < kLogoLayerCount; ++layer) {
    const uint8_t lastBlock = kLogoLayerEnds[layer];
    const uint8_t blocksInLayer = lastBlock - firstBlock;
    for (uint8_t block = firstBlock + 1; block <= lastBlock; ++block) {
      const uint32_t due = layer * kRevealLayerMs +
          static_cast<uint32_t>(block - firstBlock) * kRevealLayerMs / blocksInLayer;
      while (!elapsedAtLeast(start, due)) delay(1);
      if (!submitBeforeDeadline(block, 0U)) return false;
    }
    firstBlock = lastBlock;
  }

  // The completed graphic remains solid while only the stationary labels fade.
  for (uint8_t frame = 1; frame <= kTextFadeFrames; ++frame) {
    const uint32_t due = kLogoLayerCount * kRevealLayerMs +
        static_cast<uint32_t>(frame) * kTextFadeMs / kTextFadeFrames;
    while (!elapsedAtLeast(start, due)) delay(1);
    const uint8_t coverage =
        smoothCoverage(frame, kTextFadeFrames, 0U, kFullCoverage);
    if (!submitBeforeDeadline(kLogoBlockCount, coverage)) return false;
  }

  return true;
}

bool bootWithVideo(GfxRenderer& renderer) {
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  renderer.setRenderMode(mode);

  const esp_err_t takeoverRc =
      native_hardware_takeover_begin(T5_HARDWARE_TAKEOVER_DISPLAY);
  if (takeoverRc != ESP_OK) {
    ESP_LOGW(kBootVideoTag, "display takeover unavailable: %s",
             esp_err_to_name(takeoverRc));
    return false;
  }
  videoTakeoverActive = true;

  videoApi = t5_video_get_api(T5_VIDEO_API_VERSION);
  if (!validateVideoApi(videoApi) || !videoApi->start(&videoSurface)) {
    ESP_LOGE(kBootVideoTag, "EPD video service did not start");
    releaseVideoOwner();
    return false;
  }
  videoStarted = true;

  if (!validateVideoSurface(videoSurface)) {
    ESP_LOGE(kBootVideoTag,
             "unsupported video surface %ux%u stride=%u format=%u",
             videoSurface.width, videoSurface.height,
             videoSurface.stride_bytes, videoSurface.pixel_format);
    releaseVideoOwner();
    return false;
  }

  if (!renderLayerReveal() || !startPulseTask()) {
    ESP_LOGE(kBootVideoTag, "boot layer reveal could not enter pulse state");
    stopPulseTask();
    releaseVideoOwner();
    return false;
  }

  bootBackend = BootBackend::Video;
  return true;
}

void finishVideoBoot(GfxRenderer& renderer) {
  while (!elapsedAtLeast(pulseStartedAtMs, kMinimumPulseMs)) {
    delay(1);
  }

  stopPulseTask();

  const uint8_t startCoverage = lastPulseCoverage;
  for (uint8_t frame = 1; frame <= kFadeFrames; ++frame) {
    const uint32_t remaining = kFadeFrames - frame;
    const uint8_t coverage = static_cast<uint8_t>(
        (static_cast<uint32_t>(startCoverage) * remaining * remaining) /
        (kFadeFrames * kFadeFrames));
    if (!submitVideoFrame(kLogoBlockCount, coverage, coverage)) {
      break;
    }
  }

  waitForVideoIdle(kIdleTimeoutMs);
  releaseVideoOwner();
  renderer.requestNextRefresh(HalDisplay::FULL_REFRESH);
}

#endif

}  // namespace

void boot(GfxRenderer& renderer) {
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
  fadePending = true;
}

void finishBoot(GfxRenderer& renderer) {
  if (!fadePending) {
    return;
  }

  fadePending = false;

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
  if (bootBackend == BootBackend::Video) {
    finishVideoBoot(renderer);
    bootBackend = BootBackend::None;
    return;
  }
#endif

  if (bootBackend == BootBackend::Renderer) {
    finishRendererBoot(renderer);
  }
  bootBackend = BootBackend::None;
}

}  // namespace StartupScreen
