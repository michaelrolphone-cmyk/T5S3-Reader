#include "TextBlock.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalReadBudget.h>
#include <HalWriteBudget.h>
#include <Logging.h>
#include <Serialization.h>
#include <freertos/task.h>

#include <cstring>

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  const bool hasFocus = !wordFocusBoundary.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (hasFocus && (words.size() != wordFocusBoundary.size() || words.size() != wordFocusSuffixX.size()))) {
    LOG_ERR("TXB", "Render skipped: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)\n",
            (uint32_t)words.size(), (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size(),
            (uint32_t)wordFocusBoundary.size(), (uint32_t)wordFocusSuffixX.size());
    return;
  }

  for (size_t i = 0; i < words.size(); i++) {
    const int wordX = wordXpos[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyles[i];
    const uint8_t boundary = hasFocus ? wordFocusBoundary[i] : 0;

    if (boundary > 0) {
      char boldBuf[40];
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      const size_t boldLen = std::min<size_t>({static_cast<size_t>(boundary), words[i].size(), sizeof(boldBuf) - 1});
      memcpy(boldBuf, words[i].c_str(), boldLen);
      boldBuf[boldLen] = '\0';
      renderer.drawText(fontId, wordX, y, boldBuf, true, boldStyle);
      const int suffixX = wordX + wordFocusSuffixX[i];
      renderer.drawText(fontId, suffixX, y, words[i].c_str() + boldLen, true, currentStyle);
    } else {
      renderer.drawText(fontId, wordX, y, words[i].c_str(), true, currentStyle);
    }

    if ((currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      const std::string& w = words[i];
      const int fullWordWidth = renderer.getTextWidth(fontId, w.c_str(), currentStyle);
      const int underlineY = y + renderer.getFontAscenderSize(fontId) + 2;

      int startX = wordX;
      int underlineWidth = fullWordWidth;

      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        const int visibleWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle);
        startX = wordX + prefixWidth;
        underlineWidth = visibleWidth;
      }

      renderer.drawLine(startX, underlineY, startX + underlineWidth, underlineY, true);
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  const bool hasFocus = !wordFocusBoundary.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (hasFocus && (words.size() != wordFocusBoundary.size() || words.size() != wordFocusSuffixX.size()))) {
    LOG_ERR("TXB", "Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)\n",
            static_cast<uint32_t>(words.size()), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(wordFocusBoundary.size()),
            static_cast<uint32_t>(wordFocusSuffixX.size()));
    return false;
  }

  // Scope cooperation to one text block. Page framing keeps ordinary yields;
  // no buffering, format change, or persistent state on the file.
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  HalWriteBudget writeBudget([]() -> uint32_t { return millis(); }, []() { vTaskDelay(1); });
  const auto writePod = [&](const auto& value) { serialization::writePod(file, value, writeBudget); };
  const auto writeString = [&](const std::string& value) { serialization::writeString(file, value, writeBudget); };
#else
  const auto writePod = [&](const auto& value) { serialization::writePod(file, value); };
  const auto writeString = [&](const std::string& value) { serialization::writeString(file, value); };
#endif
  writePod(static_cast<uint16_t>(words.size()));
  for (const auto& w : words) writeString(w);
  for (auto x : wordXpos) writePod(x);
  for (auto s : wordStyles) writePod(s);
  writePod(static_cast<uint8_t>(hasFocus ? 1 : 0));
  if (hasFocus) {
    for (auto b : wordFocusBoundary) writePod(b);
    for (auto sx : wordFocusSuffixX) writePod(sx);
  }

  writePod(blockStyle.alignment);
  writePod(blockStyle.textAlignDefined);
  writePod(blockStyle.marginTop);
  writePod(blockStyle.marginBottom);
  writePod(blockStyle.marginLeft);
  writePod(blockStyle.marginRight);
  writePod(blockStyle.paddingTop);
  writePod(blockStyle.paddingBottom);
  writePod(blockStyle.paddingLeft);
  writePod(blockStyle.paddingRight);
  writePod(blockStyle.textIndent);
  writePod(blockStyle.textIndentDefined);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  // Reuse the image-cache HAL budget, scoped to this one block. Page framing
  // retains ordinary reads/yields between blocks; no file state or read-ahead.
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
  HalReadBudget readBudget([]() -> uint32_t { return millis(); }, []() { vTaskDelay(1); });
  const auto readPod = [&](auto& value) { serialization::readPod(file, value, readBudget); };
  const auto readString = [&](std::string& value) { serialization::readString(file, value, readBudget); };
#else
  const auto readPod = [&](auto& value) { serialization::readPod(file, value); };
  const auto readString = [&](std::string& value) { serialization::readString(file, value); };
#endif
  uint16_t wc;
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<uint8_t> wordFocusBoundary;
  std::vector<uint16_t> wordFocusSuffixX;
  BlockStyle blockStyle;

  readPod(wc);
  if (wc > 10000) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  for (auto& w : words) readString(w);
  for (auto& x : wordXpos) readPod(x);
  for (auto& s : wordStyles) readPod(s);

  uint8_t hasFocus = 0;
  readPod(hasFocus);
  if (hasFocus) {
    wordFocusBoundary.resize(wc);
    wordFocusSuffixX.resize(wc);
    for (auto& b : wordFocusBoundary) readPod(b);
    for (auto& sx : wordFocusSuffixX) readPod(sx);
  }

  readPod(blockStyle.alignment);
  readPod(blockStyle.textAlignDefined);
  readPod(blockStyle.marginTop);
  readPod(blockStyle.marginBottom);
  readPod(blockStyle.marginLeft);
  readPod(blockStyle.marginRight);
  readPod(blockStyle.paddingTop);
  readPod(blockStyle.paddingBottom);
  readPod(blockStyle.paddingLeft);
  readPod(blockStyle.paddingRight);
  readPod(blockStyle.textIndent);
  readPod(blockStyle.textIndentDefined);

  return std::unique_ptr<TextBlock>(new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles),
                                                  std::move(wordFocusBoundary), std::move(wordFocusSuffixX),
                                                  blockStyle));
}
