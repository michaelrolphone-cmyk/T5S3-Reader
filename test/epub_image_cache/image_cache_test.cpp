#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <GfxRenderer.h>
#include "ImageBlock.h"
#include "ImageDecoderFactory.h"
#include "DirectPixelWriter.h"
#include <Serialization.h>
#include "caller_production.inc"

// Only media I/O, image decoding and renderer surface are fixtures. The linked
// ImageBlock.cpp chooses/loads cache files, and DirectPixelWriter writes pixels.
TestStorage Storage;
struct Image { uint8_t shade; };
static std::map<std::string, Image> images;
static std::map<std::string, unsigned> decodes;
static bool failDecode = false;
static bool failCacheWrite = false;
static std::string lastCachePath;

class FixtureDecoder final : public ImageToFramebufferDecoder {
 public:
  bool decodeToFramebuffer(const std::string& path, GfxRenderer& renderer,
                           const RenderConfig& config) override {
    ++decodes[path];
    lastCachePath = config.cachePath;
    if (failDecode) return false;
    const auto found = images.find(path);
    assert(found != images.end());
    const uint8_t shade = found->second.shade;
    const int stride = (config.maxWidth + 3) / 4;
    std::vector<uint8_t> cache(4 + stride * config.maxHeight, static_cast<uint8_t>(shade * 0x55));
    const uint16_t width = config.maxWidth, height = config.maxHeight;
    std::memcpy(cache.data(), &width, 2);
    std::memcpy(cache.data() + 2, &height, 2);
    DirectPixelWriter writer;
    writer.init(renderer);
    for (int y = 0; y < config.maxHeight; ++y) {
      writer.beginRow(config.y + y);
      for (int x = 0; x < config.maxWidth; ++x) writer.writePixel(config.x + x, shade);
    }
    if (!failCacheWrite) Storage.files[config.cachePath] = cache;
    return true;
  }
  bool getDimensions(const std::string&, ImageDimensions&) const override { return false; }
  const char* getFormatName() const override { return "fixture"; }
};
bool ImageDecoderFactory::isFormatSupported(const std::string& path) {
  return path.size() >= 4 && (path.substr(path.size() - 4) == ".jpg" ||
                              path.substr(path.size() - 4) == ".png");
}
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string& path) {
  static FixtureDecoder decoder;
  return images.count(path) ? &decoder : nullptr;
}

static void reset() {
  assert(Storage.openFiles == 0);
  Storage.files.clear(); Storage.failOpen.clear(); images.clear(); decodes.clear();
  failDecode = failCacheWrite = false; lastCachePath.clear();
}
static void source(const std::string& path, uint8_t shade) {
  images[path] = {shade}; Storage.files[path] = {1};
}
static void render(const std::string& path, uint8_t shade, int width = 8, int height = 4) {
  GfxRenderer renderer;
  renderer.clear();
  ImageBlock block(path, width, height);
  block.render(renderer, 0, 0);
  const uint8_t expected = shade == 3 ? 0xff : 0x00;
  if (renderer.framebuffer[0] != expected) {
    std::fprintf(stderr, "Wrong image pixels for %s: got %02x, expected %02x\n",
                 path.c_str(), renderer.framebuffer[0], expected);
    std::abort();
  }
  assert(Storage.openFiles == 0);
}
template <typename T>
static void appendPod(std::vector<uint8_t>& bytes, const T& value) {
  const auto* begin = reinterpret_cast<const uint8_t*>(&value);
  bytes.insert(bytes.end(), begin, begin + sizeof(value));
}
static void writeSectionHeader(const std::string& path, bool embeddedStyle) {
  std::vector<uint8_t> bytes;
  appendPod(bytes, SECTION_FILE_VERSION); appendPod(bytes, int(1));
  appendPod(bytes, float(1)); appendPod(bytes, bool(false)); appendPod(bytes, uint8_t(0));
  appendPod(bytes, uint16_t(64)); appendPod(bytes, uint16_t(64));
  appendPod(bytes, bool(false)); appendPod(bytes, embeddedStyle); appendPod(bytes, uint8_t(0));
  appendPod(bytes, uint16_t(1)); Storage.files[path] = bytes;
}
static void settingReloadRegression() {
  reset();
  ParserFixture before;
  before.imageBasePath = productionImageBase(0);
  selectImage(&before, "diagram.jpg", "", "display:none");
  const auto jpeg = before.selected;
  selectImage(&before, "diagram.png", "", "");
  const auto pngBefore = before.selected;
  assert(jpeg == "/epub/img_0_0.jpg" && pngBefore == "/epub/img_0_1.png");
  source(jpeg, 0); source(pngBefore, 3);
  render(jpeg, 0); render(pngBefore, 3);

  // Actual loadSectionFile rejects the changed setting, and actual clearCache
  // removes only the page file. Cached pixels remain for subsequent reparsing.
  Section section;
  writeSectionHeader(section.filePath, false);
  assert(section.loadSectionFile(1, 1, false, 0, 64, 64, false, false, 0));
  assert(!section.loadSectionFile(1, 1, false, 0, 64, 64, false, true, 0));
  assert(!Storage.exists(section.filePath.c_str()) && Storage.openFiles == 0);

  CssParser css;
  ParserFixture after;
  after.cssParser = &css; after.imageBasePath = productionImageBase(0);
  selectImage(&after, "diagram.jpg", "", "display:none");
  assert(after.selected.empty() && after.imageCounter == 0);
  selectImage(&after, "diagram.png", "", "");
  assert(after.selected == "/epub/img_0_0.png");
  source(after.selected, 3);
  render(after.selected, 3);  // Baseline renders the earlier JPEG pixels here.
  render(after.selected, 3);
  assert(decodes[after.selected] == 1);
  // Returning to unstyled mode reuses each original source's own cache.
  render(jpeg, 0); render(pngBefore, 3);
  assert(decodes[jpeg] == 1 && decodes[pngBefore] == 1);
}
int main() {
  settingReloadRegression();
  reset();
  source("/epub/diagram.jpg", 0);
  source("/epub/diagram.png", 3);
  render("/epub/diagram.jpg", 0);
  render("/epub/diagram.png", 3);
  assert(decodes["/epub/diagram.jpg"] == 1 && decodes["/epub/diagram.png"] == 1);
  for (unsigned i = 0; i < 4; ++i) {
    render("/epub/diagram.jpg", 0);
    render("/epub/diagram.png", 3);
  }
  assert(decodes["/epub/diagram.jpg"] == 1 && decodes["/epub/diagram.png"] == 1);
  assert(Storage.files.count("/epub/diagram.jpg.pxc2"));
  assert(Storage.files.count("/epub/diagram.png.pxc2"));

  // The one-pixel cache tolerance must not bridge two different sources.
  reset();
  source("/epub/diagram.jpg", 0); source("/epub/diagram.png", 3);
  render("/epub/diagram.jpg", 0, 8, 4);
  render("/epub/diagram.png", 3, 9, 5);
  render("/epub/diagram.jpg", 0, 8, 4);
  render("/epub/diagram.png", 3, 9, 5);
  assert(decodes["/epub/diagram.jpg"] == 1 && decodes["/epub/diagram.png"] == 1);

  // Distinguish complete basenames, directories and dotted directory names.
  reset();
  const std::vector<std::string> paths = {
      "/epub/a/diagram.jpg", "/epub/b/diagram.jpg", "/epub/a/diagram.png",
      "/epub/a/diagram.jpeg", "/epub/a/diagram.v1.jpg", "/epub/a/diagram.v1.png",
      "/epub.v1/picture", "/epub.v1/other", "/epub/図版.jpg", "/epub/図版.png"};
  for (size_t i = 0; i < paths.size(); ++i) {
    const uint8_t shade = i % 2 ? 3 : 0;
    source(paths[i], shade); render(paths[i], shade);
    assert(lastCachePath == paths[i] + ".pxc2");
  }
  // Cache identity is deterministic across new ImageBlock instances and works
  // without reopening/decoding the original image on each page visit.
  for (const auto& path : paths) Storage.files.erase(path);
  for (size_t i = 0; i < paths.size(); ++i) {
    render(paths[i], i % 2 ? 3 : 0); assert(decodes[paths[i]] == 1);
  }

  // Ambiguous caches from the old key remain untouched and are never reused.
  reset();
  source("/epub/diagram.jpg", 0); render("/epub/diagram.jpg", 0);
  Storage.files["/epub/diagram.pxc"] = Storage.files.at("/epub/diagram.jpg.pxc2");
  const auto legacy = Storage.files.at("/epub/diagram.pxc");
  Storage.files.erase("/epub/diagram.jpg.pxc2");
  source("/epub/diagram.png", 3); render("/epub/diagram.png", 3);
  assert(decodes["/epub/diagram.png"] == 1);
  assert(Storage.files.at("/epub/diagram.pxc") == legacy);

  // Legacy diagram.jpg.png -> diagram.jpg.pxc must not collide with the
  // extension-preserving key for diagram.jpg. The new namespace is distinct.
  reset();
  source("/epub/diagram.jpg.png", 0); render("/epub/diagram.jpg.png", 0);
  Storage.files["/epub/diagram.jpg.pxc"] = Storage.files.at("/epub/diagram.jpg.png.pxc2");
  const auto legacyNested = Storage.files.at("/epub/diagram.jpg.pxc");
  source("/epub/diagram.jpg", 3); render("/epub/diagram.jpg", 3);
  assert(decodes["/epub/diagram.jpg"] == 1);
  assert(Storage.files.at("/epub/diagram.jpg.pxc") == legacyNested);
  assert(Storage.files.count("/epub/diagram.jpg.pxc2"));

  // Missing, truncated or unavailable cache files use the existing decoder
  // fallback, close all opened files, and become reusable after recovery.
  for (size_t retained : {size_t(0), size_t(1), size_t(3), size_t(4), size_t(5)}) {
    reset(); source("/epub/picture.png", 3); render("/epub/picture.png", 3);
    Storage.files["/epub/picture.png.pxc2"].resize(retained);
    render("/epub/picture.png", 3);
    assert(decodes["/epub/picture.png"] == 2);
    render("/epub/picture.png", 3);
    assert(decodes["/epub/picture.png"] == 2);
  }
  reset(); source("/epub/picture.png", 3); render("/epub/picture.png", 3);
  Storage.failOpen.insert("/epub/picture.png.pxc2");
  render("/epub/picture.png", 3); assert(decodes["/epub/picture.png"] == 2);
  Storage.failOpen.clear(); render("/epub/picture.png", 3);
  assert(decodes["/epub/picture.png"] == 2);

  // Dimension mismatch forces only that image to rebuild its cache.
  reset(); source("/epub/picture.png", 3); render("/epub/picture.png", 3);
  render("/epub/picture.png", 3, 12, 8);
  assert(decodes["/epub/picture.png"] == 2);
  render("/epub/picture.png", 3, 12, 8);
  assert(decodes["/epub/picture.png"] == 2);

  // Failed source open, empty source and unknown format return without a cache
  // or leaked handle. A later successful source/decoder retry still works.
  reset(); source("/epub/picture.png", 3);
  Storage.failOpen.insert("/epub/picture.png");
  render("/epub/picture.png", 3); assert(decodes.empty());
  Storage.failOpen.clear(); Storage.files["/epub/picture.png"].clear();
  render("/epub/picture.png", 3); assert(decodes.empty());
  Storage.files["/epub/picture.png"] = {1}; images.clear();
  render("/epub/picture.png", 3); assert(decodes.empty());
  source("/epub/picture.png", 3); failDecode = true;
  render("/epub/picture.png", 3); assert(decodes["/epub/picture.png"] == 1);
  assert(!Storage.files.count("/epub/picture.png.pxc2"));
  failDecode = false; render("/epub/picture.png", 3);
  assert(decodes["/epub/picture.png"] == 2);

  // A failed cache write does not prevent displaying the source; later visits
  // decode until caching recovers, then reuse the successfully written cache.
  reset(); source("/epub/picture.jpg", 0); failCacheWrite = true;
  render("/epub/picture.jpg", 0); render("/epub/picture.jpg", 0);
  assert(decodes["/epub/picture.jpg"] == 2);
  failCacheWrite = false; render("/epub/picture.jpg", 0); render("/epub/picture.jpg", 0);
  assert(decodes["/epub/picture.jpg"] == 3);

  // Real DirectPixelWriter output agrees on decoded and cached visits for all
  // supported bit planes, shades, and a non-byte-aligned width/offset.
  for (auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_MSB, GfxRenderer::GRAYSCALE_LSB}) {
    for (uint8_t shade = 0; shade < 4; ++shade) {
      reset(); source("/epub/pattern.png", shade);
      GfxRenderer renderer; renderer.mode = mode; renderer.clear();
      ImageBlock block("/epub/pattern.png", 9, 7);
      block.render(renderer, 3, 5);
      const bool drawn = mode == GfxRenderer::BW ? shade < 3 :
                         mode == GfxRenderer::GRAYSCALE_MSB ? shade == 1 || shade == 2 : shade == 1;
      const uint8_t bit = 1u << (7 - 3);
      const bool actualBit = (renderer.framebuffer[5 * 8] & bit) != 0;
      assert(actualBit == (mode == GfxRenderer::BW ? !drawn : drawn));
      const std::vector<uint8_t> decoded(std::begin(renderer.framebuffer), std::end(renderer.framebuffer));
      renderer.clear(); block.render(renderer, 3, 5);
      assert(std::equal(decoded.begin(), decoded.end(), std::begin(renderer.framebuffer)));
      assert(decodes["/epub/pattern.png"] == 1 && Storage.openFiles == 0);
    }
  }
  // Off-screen blocks must not open files or decode.
  reset(); source("/epub/picture.png", 3);
  GfxRenderer renderer; renderer.clear();
  ImageBlock block("/epub/picture.png", 8, 4);
  block.render(renderer, -1, 0); block.render(renderer, 63, 0);
  block.render(renderer, 0, -1); block.render(renderer, 0, 63);
  assert(decodes.empty() && Storage.openFiles == 0);
  std::puts("Image cache source identity, rendering, failure and retry regressions passed");
}
