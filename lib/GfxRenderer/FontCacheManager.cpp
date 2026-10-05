#include "FontCacheManager.h"

#include <FontDecompressor.h>
#include <Logging.h>
#include <SdCardFont.h>

#include <cstring>

FontCacheManager::FontCacheManager(const std::map<int, EpdFontFamily>& fontMap,
                                   const std::map<int, SdCardFont*>& sdCardFonts)
    : fontMap_(fontMap), sdCardFonts_(sdCardFonts) {}

void FontCacheManager::setFontDecompressor(FontDecompressor* d) { fontDecompressor_ = d; }

void FontCacheManager::clearCache() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
  for (auto& [id, font] : sdCardFonts_) {
    font->clearCache();
  }
}

void FontCacheManager::prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask) {
  // SD card font prewarm path: prewarm all requested styles in one call
  auto it = sdCardFonts_.find(fontId);
  if (it != sdCardFonts_.end()) {
    int missed = it->second->prewarm(utf8Text, styleMask);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache(SD): %d glyph(s) not found (styleMask=0x%02X)", missed, styleMask);
    }
    return;
  }

  // Standard compressed font prewarm path: loop over all requested styles
  if (!fontDecompressor_ || fontMap_.count(fontId) == 0) return;

  for (uint8_t i = 0; i < 4; i++) {
    if (!(styleMask & (1 << i))) continue;
    auto style = static_cast<EpdFontFamily::Style>(i);
    const EpdFontData* data = fontMap_.at(fontId).getData(style);
    if (!data || !data->groups) continue;
    int missed = fontDecompressor_->prewarmCache(data, utf8Text);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache: %d glyph(s) not cached for style %d", missed, i);
    }
  }
}

void FontCacheManager::logStats(const char* label) {
  if (fontDecompressor_) fontDecompressor_->logStats(label);
  for (auto& [id, font] : sdCardFonts_) {
    font->logStats(label);
  }
}

void FontCacheManager::resetStats() {
  if (fontDecompressor_) fontDecompressor_->resetStats();
  for (auto& [id, font] : sdCardFonts_) {
    font->resetStats();
  }
}

void FontCacheManager::resetRecordedText() { recordedTextByFont_.clear(); }

bool FontCacheManager::hasRecordedText() const { return !recordedTextByFont_.empty(); }

void FontCacheManager::prewarmRecordedText() {
  for (auto& [fontId, entry] : recordedTextByFont_) {
    if (sdCardFonts_.count(fontId) != 0) {
      // Keep SD-font aggregation and its multi-style preparation unchanged.
      uint8_t styleMask = 0;
      for (uint8_t i = 0; i < 4; i++) {
        if (entry.styleCounts[i] > 0) styleMask |= 1 << i;
      }
      prewarmCache(fontId, entry.textByStyle[0].c_str(), styleMask ? styleMask : 1);
      continue;
    }
    for (uint8_t i = 0; i < 4; i++) {
      if (!entry.textByStyle[i].empty()) {
        prewarmCache(fontId, entry.textByStyle[i].c_str(), 1 << i);
      }
    }
  }

  resetRecordedText();
}

bool FontCacheManager::isScanning() const { return scanMode_ == ScanMode::Scanning; }

void FontCacheManager::recordText(const char* text, int fontId, EpdFontFamily::Style style) {
  if (text == nullptr || *text == '\0' || fontId == 0) {
    return;
  }

  const uint8_t baseStyle = static_cast<uint8_t>(style) & 0x03;
  uint8_t textStyle = baseStyle;
  const bool isSdFont = sdCardFonts_.count(fontId) != 0;
  if (isSdFont) {
    textStyle = 0;
  } else {
    const auto font = fontMap_.find(fontId);
    if (font != fontMap_.end()) {
      const auto* data = font->second.getData(static_cast<EpdFontFamily::Style>(baseStyle));
      // Missing variants and explicit aliases share one decompressor slot.
      // Append in draw order so every glyph for that actual font is retained.
      for (uint8_t i = 0; i < baseStyle; i++) {
        if (font->second.getData(static_cast<EpdFontFamily::Style>(i)) == data) {
          textStyle = i;
          break;
        }
      }
    }
  }

  auto& entry = recordedTextByFont_[fontId];
  auto& recorded = entry.textByStyle[textStyle];
  if (recorded.empty() && textStyle == 0) {
    recorded.reserve(2048);  // Retain the regular-body reserve; short styled spans grow only as needed.
  }
  recorded += text;
  if (isSdFont) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    uint32_t cpCount = 0;
    while (*p) {
      if ((*p & 0xC0) != 0x80) cpCount++;
      p++;
    }
    entry.styleCounts[baseStyle] += cpCount;
  }
}

// --- PrewarmScope implementation ---

FontCacheManager::PrewarmScope::PrewarmScope(FontCacheManager& manager) : manager_(&manager) {
  manager_->scanMode_ = ScanMode::Scanning;
  manager_->clearCache();
  manager_->resetStats();
  manager_->resetRecordedText();
}

void FontCacheManager::PrewarmScope::endScanAndPrewarm() {
  manager_->scanMode_ = ScanMode::None;
  if (!manager_->hasRecordedText()) return;
  manager_->prewarmRecordedText();
}

FontCacheManager::PrewarmScope::~PrewarmScope() {
  if (active_) {
    endScanAndPrewarm();  // no-op if already called (scanText_ is empty)
    manager_->clearCache();
  }
}

FontCacheManager::PrewarmScope::PrewarmScope(PrewarmScope&& other) noexcept
    : manager_(other.manager_), active_(other.active_) {
  other.active_ = false;
}

FontCacheManager::PrewarmScope FontCacheManager::createPrewarmScope() { return PrewarmScope(*this); }
