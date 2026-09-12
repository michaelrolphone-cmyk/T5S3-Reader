#include "TxtReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Markdown.h>

#include <algorithm>
#include <cstdlib>
#include <variant>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "MarkdownChapterSelectionActivity.h"
#include "components/UITheme.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;

bool isUtf8ContinuationByte(char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

size_t nextUtf8Boundary(const std::string& text, size_t pos) {
  if (pos >= text.size()) {
    return text.size();
  }
  ++pos;
  while (pos < text.size() && isUtf8ContinuationByte(text[pos])) {
    ++pos;
  }
  return pos;
}
}  // namespace

bool TxtReaderActivity::loadPageAtOffset(size_t offset, bool fenceOpen, std::vector<TxtDisplayLine>& outLines,
                                        size_t& nextOffset, bool& fenceOpenAfter) {
  outLines.clear();
  fenceOpenAfter = fenceOpen;
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), 0x01);
  }

  size_t pos = 0;
  uint32_t wrapSteps = 0;
  int usedHeight = 0;

  auto styleOf = [](uint8_t headingLevel) {
    return headingLevel > 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  };

  auto textWidth = [&](const std::string& text, uint8_t headingLevel) {
    return renderer.getTextAdvanceX(cachedFontId, text.c_str(), styleOf(headingLevel));
  };

  auto heightOf = [&](uint8_t headingLevel) {
    const int base = renderer.getLineHeight(cachedFontId);
    if (headingLevel == 1) {
      return base + base / 2;
    }
    if (headingLevel >= 2) {
      return base + base / 4;
    }
    return base;
  };

  auto findWrapBreak = [&](const std::string& text, uint8_t headingLevel) -> size_t {
    std::vector<size_t> utf8Boundaries;
    utf8Boundaries.reserve(std::min<size_t>(text.size(), 512));
    for (size_t p = 0; p < text.size();) {
      p = nextUtf8Boundary(text, p);
      utf8Boundaries.push_back(p);
    }

    if (utf8Boundaries.empty()) {
      return 0;
    }

    size_t low = 0;
    size_t high = utf8Boundaries.size();
    while (low < high) {
      const size_t mid = (low + high + 1) / 2;
      const size_t bytes = utf8Boundaries[mid - 1];
      if (textWidth(text.substr(0, bytes), headingLevel) <= viewportWidth) {
        low = mid;
      } else {
        high = mid - 1;
      }
    }

    const size_t maxFit = (low == 0) ? utf8Boundaries.front() : utf8Boundaries[low - 1];
    if (maxFit < text.length()) {
      const size_t spacePos = text.rfind(' ', maxFit > 0 ? maxFit - 1 : 0);
      if (spacePos != std::string::npos && spacePos > 0) {
        return spacePos;
      }
    }

    return maxFit;
  };

  auto pageFull = [&](int extraHeight) {
    if (markdownMode) {
      return usedHeight + extraHeight > viewportHeight && !outLines.empty();
    }
    return static_cast<int>(outLines.size()) >= linesPerPage;
  };

  bool inFence = fenceOpen;

  while (pos < chunkSize) {
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && !outLines.empty()) {
      break;
    }

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string source(reinterpret_cast<char*>(buffer + pos), displayLen);
    uint8_t headingLevel = 0;
    std::string line = source;

    if (markdownMode) {
      Markdown::Line parsed = Markdown::parseLine(source, inFence);
      if (parsed.chapterBreak && !outLines.empty()) {
        break;
      }
      headingLevel = parsed.headingLevel;
      line = parsed.display;
      if (parsed.kind == Markdown::BlockKind::Empty) {
        if (!outLines.empty() && !outLines.back().text.empty()) {
          const int h = heightOf(0);
          if (pageFull(h)) {
            break;
          }
          outLines.push_back({"", 0});
          usedHeight += h;
        }
        pos = lineEnd + 1;
        continue;
      }
    }

    size_t lineBytePos = 0;

    if (line.empty()) {
      const int h = heightOf(headingLevel);
      if (pageFull(h)) {
        break;
      }
      outLines.push_back({"", headingLevel});
      usedHeight += h;
      pos = lineEnd + 1;
      continue;
    }

    while (!line.empty()) {
      const int h = heightOf(headingLevel);
      if (pageFull(h)) {
        break;
      }

      const int lineWidth = textWidth(line, headingLevel);
      if (lineWidth <= viewportWidth) {
        outLines.push_back({line, headingLevel});
        usedHeight += h;
        lineBytePos = displayLen;
        line.clear();
        break;
      }

      const size_t breakPos = findWrapBreak(line, headingLevel);
      if (breakPos == 0) break;

      outLines.push_back({line.substr(0, breakPos), headingLevel});
      usedHeight += h;

      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);

      if ((++wrapSteps & 0x0F) == 0) {
        vTaskDelay(1);
      }
    }

    if (line.empty()) {
      pos = lineEnd + 1;
    } else {
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) {
    pos = 1;
  }

  nextOffset = offset + pos;
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }
  fenceOpenAfter = inFence;

  free(buffer);
  return !outLines.empty() || nextOffset > offset;
}

int TxtReaderActivity::lineHeightFor(const TxtDisplayLine& line) const {
  const int base = renderer.getLineHeight(cachedFontId);
  if (line.headingLevel == 1) {
    return base + base / 2;
  }
  if (line.headingLevel >= 2) {
    return base + base / 4;
  }
  return base;
}

EpdFontFamily::Style TxtReaderActivity::styleFor(const TxtDisplayLine& line) const {
  return line.headingLevel > 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
}

bool TxtReaderActivity::peekMarkdownLine(size_t offset, Markdown::Line& outLine) const {
  const size_t fileSize = txt->getFileSize();
  if (offset >= fileSize) {
    return false;
  }
  const size_t chunkSize = std::min<size_t>(256, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer || !txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  size_t len = 0;
  while (len < chunkSize && buffer[len] != '\n') {
    ++len;
  }
  if (len > 0 && buffer[len - 1] == '\r') {
    --len;
  }
  std::string source(reinterpret_cast<char*>(buffer), len);
  free(buffer);
  bool fence = false;
  outLine = Markdown::parseLine(source, fence);
  return true;
}

void TxtReaderActivity::collectChapters() {
  chapters.clear();
  if (!markdownMode || pageOffsets.empty()) {
    return;
  }

  for (size_t i = 0; i < pageOffsets.size(); ++i) {
    Markdown::Line parsed;
    if (!peekMarkdownLine(pageOffsets[i], parsed) || !parsed.chapterBreak) {
      continue;
    }
    Markdown::Chapter chapter;
    chapter.title = parsed.display.empty() ? txt->getTitle() : parsed.display;
    chapter.fileOffset = pageOffsets[i];
    chapter.startPage = static_cast<int>(i);
    chapters.push_back(std::move(chapter));
  }

  if (chapters.empty() || chapters.front().startPage > 0) {
    Markdown::Chapter intro;
    intro.title = txt->getTitle();
    intro.fileOffset = 0;
    intro.startPage = 0;
    chapters.insert(chapters.begin(), std::move(intro));
  }

  LOG_DBG("TRS", "Markdown chapters: %d", static_cast<int>(chapters.size()));
}

const Markdown::Chapter* TxtReaderActivity::chapterForPage(int page) const {
  const Markdown::Chapter* found = nullptr;
  for (const auto& chapter : chapters) {
    if (page >= chapter.startPage) {
      found = &chapter;
    }
  }
  return found;
}

void TxtReaderActivity::openChapterList() {
  if (chapters.empty()) {
    collectChapters();
  }
  startActivityForResult(
      std::make_unique<MarkdownChapterSelectionActivity>(renderer, mappedInput, chapters, currentPage),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        if (const auto* page = std::get_if<PageResult>(&result.data)) {
          currentPage = static_cast<int>(page->page);
          if (currentPage < 0) currentPage = 0;
          if (currentPage >= totalPages) currentPage = totalPages - 1;
          requestUpdate();
        }
      });
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
