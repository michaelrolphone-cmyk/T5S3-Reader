#include <T5AppApi.h>
#include <T5ImageApi.h>

#include <Arduino.h>
#include <GfxRenderer.h>
#include <JPEGDEC.h>
#include <NativeAppLauncher.h>
#include <PNGdec.h>
#include <esp_err.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "MappedInputManager.h"
#include "NativeAppHost.h"
#include "NativeSystemUiBridge.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {
constexpr size_t kMaxImageBytes = 12u * 1024u * 1024u;
constexpr const char* kViewerPath = "/sd/Apps/image_viewer.elf";

struct ViewerResult {
  bool available = false;
  int32_t error = 0;
  uint64_t cookie = 0;
};
ViewerResult viewerResult;
std::string activeSourcePath;

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

bool validSdPath(const char* path) {
  if (!path || std::strncmp(path, "/sd/", 4) != 0 || !path[4]) return false;
  const size_t n = std::strlen(path);
  if (n >= T5_IMAGE_PATH_MAX) return false;
  for (size_t i = 0; i < n; ++i) {
    if (static_cast<unsigned char>(path[i]) < 32) return false;
  }
  return true;
}

bool imageExtension(const char* path) {
  if (!path) return false;
  std::string s(path);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s.ends_with(".jpg") || s.ends_with(".jpeg") || s.ends_with(".png") || s.ends_with(".bmp");
}

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
int32_t sle32(const uint8_t* p) { return static_cast<int32_t>(le32(p)); }

struct FileBuffer {
  uint8_t* data = nullptr;
  size_t size = 0;
  ~FileBuffer() { if (data) free(data); }
};

bool readImage(const char* path, FileBuffer& out) {
  if (!validSdPath(path)) return false;
  FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
  const long length = std::ftell(f);
  if (length <= 0 || static_cast<size_t>(length) > kMaxImageBytes || std::fseek(f, 0, SEEK_SET) != 0) {
    std::fclose(f); return false;
  }
  out.data = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(length)));
  if (!out.data) out.data = static_cast<uint8_t*>(malloc(static_cast<size_t>(length)));
  if (!out.data) { std::fclose(f); return false; }
  out.size = static_cast<size_t>(length);
  const bool ok = std::fread(out.data, 1, out.size, f) == out.size;
  std::fclose(f);
  if (!ok) { free(out.data); out.data = nullptr; out.size = 0; }
  return ok;
}

t5_image_format_t sniff(const FileBuffer& file) {
  if (file.size >= 8 && !std::memcmp(file.data, "\x89PNG\r\n\x1a\n", 8)) return T5_IMAGE_FORMAT_PNG;
  if (file.size >= 2 && file.data[0] == 0xff && file.data[1] == 0xd8) return T5_IMAGE_FORMAT_JPEG;
  if (file.size >= 2 && file.data[0] == 'B' && file.data[1] == 'M') return T5_IMAGE_FORMAT_BMP;
  return T5_IMAGE_FORMAT_UNKNOWN;
}

bool bmpInfo(const FileBuffer& file, t5_image_info_t& out) {
  if (file.size < 54 || sniff(file) != T5_IMAGE_FORMAT_BMP) return false;
  const uint32_t dib = le32(file.data + 14);
  if (dib < 40 || 14u + dib > file.size) return false;
  const int32_t w = sle32(file.data + 18);
  const int32_t h = sle32(file.data + 22);
  if (w <= 0 || h == 0) return false;
  out.width = static_cast<uint32_t>(w);
  out.height = static_cast<uint32_t>(h < 0 ? -static_cast<int64_t>(h) : h);
  out.format = T5_IMAGE_FORMAT_BMP;
  return out.width && out.height;
}

bool probeBuffer(FileBuffer& file, t5_image_info_t& out) {
  out = {};
  switch (sniff(file)) {
    case T5_IMAGE_FORMAT_PNG: {
      PNG png;
      if (png.openRAM(file.data, static_cast<int>(file.size), nullptr) != PNG_SUCCESS) return false;
      out.width = static_cast<uint32_t>(png.getWidth());
      out.height = static_cast<uint32_t>(png.getHeight());
      out.format = T5_IMAGE_FORMAT_PNG;
      png.close();
      return out.width && out.height;
    }
    case T5_IMAGE_FORMAT_JPEG: {
      JPEGDEC jpeg;
      if (!jpeg.openRAM(file.data, static_cast<int>(file.size), nullptr)) return false;
      out.width = static_cast<uint32_t>(jpeg.getWidth());
      out.height = static_cast<uint32_t>(jpeg.getHeight());
      out.format = T5_IMAGE_FORMAT_JPEG;
      jpeg.close();
      return out.width && out.height;
    }
    case T5_IMAGE_FORMAT_BMP:
      return bmpInfo(file, out);
    default:
      return false;
  }
}

bool probe(const char* path, t5_image_info_t* out) {
  if (!active() || !out || !imageExtension(path)) return false;
  FileBuffer file;
  return readImage(path, file) && probeBuffer(file, *out);
}

uint8_t gray565(uint16_t pixel) {
  const uint32_t r = ((pixel >> 11) & 0x1f) * 255u / 31u;
  const uint32_t g = ((pixel >> 5) & 0x3f) * 255u / 63u;
  const uint32_t b = (pixel & 0x1f) * 255u / 31u;
  return static_cast<uint8_t>((r * 77u + g * 150u + b * 29u) >> 8);
}

struct DecodeTarget {
  uint8_t* gray = nullptr;
  uint16_t* line = nullptr;
  int sw = 0;
  int sh = 0;
  int dw = 0;
  int dh = 0;
  PNG* png = nullptr;
};

int pngDraw(PNGDRAW* draw) {
  auto* t = static_cast<DecodeTarget*>(draw->pUser);
  if (!t || !t->png || !t->gray || !t->line) return 0;
  t->png->getLineAsRGB565(draw, t->line, PNG_RGB565_LITTLE_ENDIAN, 0xffffffffu);
  const int dy = draw->y * t->dh / t->sh;
  if (dy < 0 || dy >= t->dh) return 1;
  for (int sx = 0; sx < std::min(draw->iWidth, t->sw); ++sx) {
    const int dx = sx * t->dw / t->sw;
    if (dx >= 0 && dx < t->dw) t->gray[dy * t->dw + dx] = gray565(t->line[sx]);
  }
  return 1;
}

int jpegDraw(JPEGDRAW* draw) {
  auto* t = static_cast<DecodeTarget*>(draw->pUser);
  if (!t || !t->gray || !draw->pPixels) return 0;
  for (int by = 0; by < draw->iHeight; ++by) {
    const int sy = draw->y + by;
    if (sy < 0 || sy >= t->sh) continue;
    const int dy = sy * t->dh / t->sh;
    for (int bx = 0; bx < draw->iWidth; ++bx) {
      const int sx = draw->x + bx;
      if (sx < 0 || sx >= t->sw) continue;
      const int dx = sx * t->dw / t->sw;
      t->gray[dy * t->dw + dx] = gray565(draw->pPixels[by * draw->iWidth + bx]);
    }
  }
  return 1;
}

bool decodeBmp(const FileBuffer& file, DecodeTarget& t) {
  t5_image_info_t info{};
  if (!bmpInfo(file, info)) return false;
  const uint32_t dataOffset = le32(file.data + 10);
  const uint32_t dib = le32(file.data + 14);
  const int32_t signedHeight = sle32(file.data + 22);
  const uint16_t planes = le16(file.data + 26);
  const uint16_t bpp = le16(file.data + 28);
  const uint32_t compression = le32(file.data + 30);
  if (planes != 1 || dataOffset >= file.size || compression != 0 ||
      (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)) return false;
  const bool topDown = signedHeight < 0;
  const uint32_t rowBytes = ((info.width * bpp + 31u) / 32u) * 4u;
  if (rowBytes == 0 || dataOffset + static_cast<uint64_t>(rowBytes) * info.height > file.size) return false;
  const uint8_t* palette = file.data + 14 + dib;
  const uint32_t paletteCount = bpp <= 8 ? (1u << bpp) : 0u;
  if (paletteCount && palette + paletteCount * 4u > file.data + dataOffset) return false;
  for (uint32_t sy = 0; sy < info.height; ++sy) {
    const uint32_t fileY = topDown ? sy : info.height - 1u - sy;
    const uint8_t* row = file.data + dataOffset + static_cast<size_t>(fileY) * rowBytes;
    const int dy = static_cast<int>(sy) * t.dh / t.sh;
    for (uint32_t sx = 0; sx < info.width; ++sx) {
      uint8_t r = 0, g = 0, b = 0;
      if (bpp == 24) {
        const uint8_t* p = row + sx * 3u; b = p[0]; g = p[1]; r = p[2];
      } else if (bpp == 32) {
        const uint8_t* p = row + sx * 4u; b = p[0]; g = p[1]; r = p[2];
      } else if (bpp == 16) {
        const uint16_t p = le16(row + sx * 2u);
        r = static_cast<uint8_t>(((p >> 10) & 31u) * 255u / 31u);
        g = static_cast<uint8_t>(((p >> 5) & 31u) * 255u / 31u);
        b = static_cast<uint8_t>((p & 31u) * 255u / 31u);
      } else {
        uint32_t idx = 0;
        if (bpp == 8) idx = row[sx];
        else if (bpp == 4) idx = (sx & 1u) ? (row[sx >> 1] & 0x0fu) : (row[sx >> 1] >> 4);
        else idx = (row[sx >> 3] >> (7u - (sx & 7u))) & 1u;
        const uint8_t* p = palette + idx * 4u; b = p[0]; g = p[1]; r = p[2];
      }
      const int dx = static_cast<int>(sx) * t.dw / t.sw;
      t.gray[dy * t.dw + dx] = static_cast<uint8_t>((r * 77u + g * 150u + b * 29u) >> 8);
    }
    if ((sy & 31u) == 0) esp_task_wdt_reset();
  }
  return true;
}

bool renderFit(const char* path, int32_t x, int32_t y, int32_t width, int32_t height) {
  if (!active() || !imageExtension(path) || width <= 0 || height <= 0) return false;
  FileBuffer file;
  t5_image_info_t info{};
  if (!readImage(path, file) || !probeBuffer(file, info)) return false;
  const double scale = std::min(1.0, std::min(static_cast<double>(width) / info.width,
                                             static_cast<double>(height) / info.height));
  const int dw = std::max(1, static_cast<int>(info.width * scale));
  const int dh = std::max(1, static_cast<int>(info.height * scale));
  auto* gray = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(dw) * dh));
  if (!gray) gray = static_cast<uint8_t*>(malloc(static_cast<size_t>(dw) * dh));
  if (!gray) return false;
  std::memset(gray, 255, static_cast<size_t>(dw) * dh);
  DecodeTarget target{gray, nullptr, static_cast<int>(info.width), static_cast<int>(info.height), dw, dh, nullptr};
  bool decoded = false;

  if (info.format == T5_IMAGE_FORMAT_PNG) {
    PNG png;
    target.png = &png;
    target.line = static_cast<uint16_t*>(ps_malloc(static_cast<size_t>(info.width) * sizeof(uint16_t)));
    if (!target.line) target.line = static_cast<uint16_t*>(malloc(static_cast<size_t>(info.width) * sizeof(uint16_t)));
    if (target.line && png.openRAM(file.data, static_cast<int>(file.size), pngDraw) == PNG_SUCCESS) {
      decoded = png.decode(&target, 0) == PNG_SUCCESS;
      png.close();
    }
    if (target.line) free(target.line);
  } else if (info.format == T5_IMAGE_FORMAT_JPEG) {
    JPEGDEC jpeg;
    if (jpeg.openRAM(file.data, static_cast<int>(file.size), jpegDraw)) {
      jpeg.setUserPointer(&target);
      jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
      decoded = jpeg.decode(0, 0, 0) != 0;
      jpeg.close();
    }
  } else if (info.format == T5_IMAGE_FORMAT_BMP) {
    decoded = decodeBmp(file, target);
  }

  if (decoded) {
    static const uint8_t bayer[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
    GfxRenderer& gfx = activityManager.nativeAppRenderer();
    const int ox = x + (width - dw) / 2;
    const int oy = y + (height - dh) / 2;
    for (int py = 0; py < dh; ++py) {
      for (int px = 0; px < dw; ++px) {
        const uint8_t threshold = bayer[((py & 3) << 2) | (px & 3)];
        if (gray[py * dw + px] < threshold) gfx.drawPixel(ox + px, oy + py, true);
      }
      if ((py & 31) == 0) esp_task_wdt_reset();
    }
  }
  free(gray);
  return decoded;
}

class NativeImageViewerActivity final : public Activity {
  std::string resumePath;
  std::string sourcePath;
  uint64_t cookie;
  bool ranViewer = false;
  bool resumeReturned = false;
 public:
  NativeImageViewerActivity(GfxRenderer& gfxRenderer, MappedInputManager& input, std::string resume,
                            std::string source, uint64_t requestCookie)
      : Activity("NativeImageViewer", gfxRenderer, input), resumePath(std::move(resume)),
        sourcePath(std::move(source)), cookie(requestCookie) {}
  void loop() override {
    if (resumeReturned) { finish(); return; }
    if (!ranViewer) {
      ranViewer = true;
      activeSourcePath = sourcePath;
      viewerResult.available = true;
      viewerResult.error = runNativeApp(kViewerPath, renderer, mappedInput);
      viewerResult.cookie = cookie;
      activeSourcePath.clear();
    }
    if (resumePath.empty() || runNativeApp(resumePath.c_str(), renderer, mappedInput) != ESP_OK) {
      viewerResult = {};
      finish();
      return;
    }
    resumeReturned = true;
  }
  void render(RenderLock&&) override {}
};

bool viewerOpenRequest(const char* sourcePath, uint64_t cookie) {
  const char* currentPath = native_app_current_path();
  if (!active() || !validSdPath(currentPath) || !validSdPath(sourcePath) || !imageExtension(sourcePath) || viewerResult.available)
    return false;
  FILE* f = std::fopen(sourcePath, "rb");
  if (!f) return false;
  std::fclose(f);
  activityManager.pushActivity(std::make_unique<NativeImageViewerActivity>(
      renderer, mappedInputManager, std::string(currentPath), std::string(sourcePath), cookie));
  nativeSystemUiMarkActivityPending();
  return true;
}

bool viewerOpenTakeResult(int32_t* error, uint64_t* cookie) {
  if (!viewerResult.available) return false;
  if (error) *error = viewerResult.error;
  if (cookie) *cookie = viewerResult.cookie;
  viewerResult = {};
  return true;
}

bool sourcePathGet(char* out, size_t capacity) {
  if (!active() || !out || capacity == 0 || activeSourcePath.empty() || activeSourcePath.size() >= capacity) return false;
  std::memcpy(out, activeSourcePath.c_str(), activeSourcePath.size() + 1);
  return true;
}

const t5_image_api_v1 api = {T5_IMAGE_API_VERSION, sizeof(t5_image_api_v1), viewerOpenRequest,
                             viewerOpenTakeResult, sourcePathGet, probe, renderFit};
}  // namespace

extern "C" const t5_image_api_v1* t5_image_get_api(uint32_t version) {
  return version == T5_IMAGE_API_VERSION && active() ? &api : nullptr;
}
