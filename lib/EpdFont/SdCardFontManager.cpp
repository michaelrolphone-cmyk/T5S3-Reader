#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>

#include <cstdlib>

namespace {

constexpr uint8_t kUiContentFontRole = 0xFF;

const SdCardFontFileInfo* selectReaderFile(const SdCardFontFamilyInfo& family, uint8_t fontSizeEnum) {
  auto sizes = family.availableSizes();
  if (sizes.empty()) {
    return nullptr;
  }

  uint8_t idx = fontSizeEnum;
  if (idx >= sizes.size()) {
    idx = sizes.size() - 1;
  }
  return family.findFile(sizes[idx]);
}

const SdCardFontFileInfo* selectClosestPointSizeFile(const SdCardFontFamilyInfo& family, uint8_t targetPointSize) {
  auto sizes = family.availableSizes();
  if (sizes.empty()) {
    return nullptr;
  }

  uint8_t bestSize = sizes.front();
  int bestDelta = std::abs(static_cast<int>(bestSize) - static_cast<int>(targetPointSize));
  for (const auto size : sizes) {
    const int delta = std::abs(static_cast<int>(size) - static_cast<int>(targetPointSize));
    if (delta < bestDelta || (delta == bestDelta && size < bestSize)) {
      bestSize = size;
      bestDelta = delta;
    }
  }

  return family.findFile(bestSize);
}

}  // namespace

SdCardFontManager::~SdCardFontManager() {
  for (auto& lf : loaded_) {
    delete lf.font;
  }
}

// FNV-1a continuation: seeds with contentHash, then hashes family name + point size.
// Produces a deterministic ID that is stable across load/unload cycles and reboots,
// and changes when font content changes (different header/TOC = different contentHash).
int SdCardFontManager::computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize) {
  static constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = contentHash;
  while (*familyName) {
    hash ^= static_cast<uint8_t>(*familyName++);
    hash *= FNV_PRIME;
  }
  hash ^= pointSize;
  hash *= FNV_PRIME;
  int id = static_cast<int>(hash);
  return id != 0 ? id : 1;  // 0 is reserved as "not found" sentinel
}

bool SdCardFontManager::loadFontFile(const SdCardFontFamilyInfo& family, const SdCardFontFileInfo& fileInfo,
                                     GfxRenderer& renderer, int& outFontId) {
  auto* font = new (std::nothrow) SdCardFont();
  if (!font) {
    LOG_ERR("SDMGR", "Failed to allocate SdCardFont for %s", fileInfo.path.c_str());
    return false;
  }

  if (!font->load(fileInfo.path.c_str())) {
    LOG_ERR("SDMGR", "Failed to load %s", fileInfo.path.c_str());
    delete font;
    return false;
  }

  const int fontId = computeFontId(font->contentHash(), family.name.c_str(), fileInfo.pointSize);
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, fileInfo.path.c_str());
    delete font;
    return false;
  }

  renderer.registerSdCardFont(fontId, font);
  loaded_.push_back({font, fontId, fileInfo.pointSize});

  LOG_DBG("SDMGR", "Loaded %s size=%u id=%d styles=%u", fileInfo.path.c_str(), fileInfo.pointSize, fontId,
          font->styleCount());

  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);
  outFontId = fontId;
  return true;
}

bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                   uint8_t readerFontSizeEnum, uint8_t uiContentPointSize) {
  if (!loadedFamilyName_.empty()) {
    unloadAll(renderer);
  }

  const auto* readerFile = selectReaderFile(family, readerFontSizeEnum);
  const auto* uiContentFile = selectClosestPointSizeFile(family, uiContentPointSize);
  if (!readerFile || !uiContentFile) {
    LOG_ERR("SDMGR", "Family %s has no files to load", family.name.c_str());
    return false;
  }

  if (!loadFontFile(family, *readerFile, renderer, readerFontId_)) {
    return false;
  }

  loadedReaderSizeEnum_ = readerFontSizeEnum;
  loadedReaderPointSize_ = readerFile->pointSize;

  if (uiContentFile->pointSize == readerFile->pointSize) {
    uiContentFontId_ = readerFontId_;
    loadedUiContentPointSize_ = readerFile->pointSize;
  } else {
    if (!loadFontFile(family, *uiContentFile, renderer, uiContentFontId_)) {
      unloadAll(renderer);
      return false;
    }
    loadedUiContentPointSize_ = uiContentFile->pointSize;
  }

  loadedFamilyName_ = family.name;
  return true;
}

void SdCardFontManager::unloadAll(GfxRenderer& renderer) {
  for (auto& lf : loaded_) {
    renderer.removeFont(lf.fontId);
    delete lf.font;
  }
  loaded_.clear();
  loadedFamilyName_.clear();
  loadedReaderSizeEnum_ = 0;
  loadedReaderPointSize_ = 0;
  loadedUiContentPointSize_ = 0;
  readerFontId_ = 0;
  uiContentFontId_ = 0;
}

int SdCardFontManager::getFontId(const std::string& familyName, uint8_t fontSizeEnum) const {
  if (familyName != loadedFamilyName_ || loaded_.empty()) return 0;
  if (fontSizeEnum == kUiContentFontRole) {
    return uiContentFontId_;
  }
  if (fontSizeEnum == loadedReaderSizeEnum_) {
    return readerFontId_;
  }
  return 0;
}

int SdCardFontManager::getUiContentFontId(const std::string& familyName) const {
  if (familyName != loadedFamilyName_ || loaded_.empty()) return 0;
  return uiContentFontId_;
}
