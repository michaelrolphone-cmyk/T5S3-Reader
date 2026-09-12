#include "TxtReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Markdown.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 4;
constexpr char UTF8_ELLIPSIS[] = "\xE2\x80\xA6";
}  // namespace

void TxtReaderActivity::onEnter() {
  Activity::onEnter();
  if (!txt) {
    return;
  }
  pagesUntilFullRefresh = ReaderUtils::initialPagesUntilFullRefresh(initialRefreshMode);
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  txt->setupCacheDir();
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  Activity::onExit();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  pageOffsets.clear();
  pageFenceOpen.clear();
  currentPageLines.clear();
  chapters.clear();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  txt.reset();
}

void TxtReaderActivity::loop() {
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(txt ? txt->getPath() : "");
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    onGoHome();
    return;
  }
  if (markdownMode && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openChapterList();
    return;
  }
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }
  if (ReaderUtils::isPageTurnInputBlocked()) {
    return;
  }
  if (prevTriggered && currentPage > 0) {
    currentPage--;
    ReaderUtils::requestPageTurnEffect(renderer, false);
    requestUpdate();
  } else if (nextTriggered) {
    if (currentPage < totalPages - 1) {
      currentPage++;
      ReaderUtils::requestPageTurnEffect(renderer, true);
      requestUpdate();
    } else {
      finishReadingAndGoHome();
    }
  }
}

bool TxtReaderActivity::onTouchTap(int16_t x, int16_t) {
  const int width = renderer.getScreenWidth();
  if (x < width / 3) {
    if (ReaderUtils::isPageTurnInputBlocked()) {
      return true;
    }
    if (currentPage > 0) {
      currentPage--;
      ReaderUtils::requestPageTurnEffect(renderer, false);
      requestUpdate();
    }
    return true;
  }
  if (x > (width * 2) / 3) {
    if (ReaderUtils::isPageTurnInputBlocked()) {
      return true;
    }
    if (currentPage < totalPages - 1) {
      currentPage++;
      ReaderUtils::requestPageTurnEffect(renderer, true);
      requestUpdate();
    } else {
      finishReadingAndGoHome();
    }
    return true;
  }
  return false;
}

void TxtReaderActivity::finishReadingAndGoHome() {
  if (SETTINGS.autoRemoveFinishedRecentBooks && txt) {
    RECENT_BOOKS.removeBook(txt->getPath());
  }
  onGoHome();
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;
  markdownMode = txt && FsHelpers::hasMarkdownExtension(txt->getPath());
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));
  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;
  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d, markdown=%d", viewportWidth, viewportHeight, linesPerPage,
          markdownMode ? 1 : 0);
  if (markdownMode || !loadPageIndexCache()) {
    buildPageIndex();
    if (!markdownMode) {
      savePageIndexCache();
    }
  }
  collectChapters();
  loadProgress();
  initialized = true;
}

void TxtReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  pageFenceOpen.clear();
  pageOffsets.push_back(0);
  pageFenceOpen.push_back(0);
  size_t offset = 0;
  bool fenceOpen = false;
  const size_t fileSize = txt->getFileSize();
  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);
  GUI.drawPopup(renderer, tr(STR_INDEXING));
  while (offset < fileSize) {
    std::vector<TxtDisplayLine> tempLines;
    size_t nextOffset = offset;
    bool fenceAfter = fenceOpen;
    if (!loadPageAtOffset(offset, fenceOpen, tempLines, nextOffset, fenceAfter)) {
      break;
    }
    if (nextOffset <= offset) {
      break;
    }
    offset = nextOffset;
    fenceOpen = fenceAfter;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
      pageFenceOpen.push_back(fenceOpen ? 1 : 0);
    }
    if ((pageOffsets.size() & 0x07) == 0) {
      vTaskDelay(1);
    }
  }
  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

void TxtReaderActivity::render(RenderLock&&) {
  if (!txt) {
    return;
  }
  if (!initialized) {
    initializeReader();
  }
  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  bool fenceAfter = false;
  const bool fenceOpen = currentPage < static_cast<int>(pageFenceOpen.size()) && pageFenceOpen[currentPage];
  currentPageLines.clear();
  loadPageAtOffset(offset, fenceOpen, currentPageLines, nextOffset, fenceAfter);
  renderer.clearScreen();
  renderPage();
  saveProgress();
}

void TxtReaderActivity::renderPage() {
  const int contentWidth = viewportWidth;
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      const auto style = styleFor(line);
      if (!line.text.empty()) {
        int x = cachedOrientedMarginLeft;
        switch (cachedParagraphAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.text.c_str(), style);
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.text.c_str(), style);
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }
        renderer.drawText(cachedFontId, x, y, line.text.c_str(), true, style);
      }
      y += lineHeightFor(line);
    }
  };
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();
  std::string statusTitle;
  TextRole statusTitleRole = TextRole::System;
  buildStatusBarTitle(statusTitle, statusTitleRole);
  if (!statusTitle.empty()) {
    const int statusFontId = BaseTheme::resolveTextFontId(SMALL_FONT_ID, statusTitleRole);
    fcm->recordText(statusTitle.c_str(), statusFontId, EpdFontFamily::REGULAR);
    fcm->recordText(UTF8_ELLIPSIS, statusFontId, EpdFontFamily::REGULAR);
  }
  scope.endScanAndPrewarm();
  renderLines();
  renderStatusBar();
  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, pagesUntilFullRefresh, [&renderLines]() { renderLines(); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}

void TxtReaderActivity::buildStatusBarTitle(std::string& title, TextRole& titleRole) const {
  title.clear();
  titleRole = TextRole::System;
  if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    return;
  }
  if (markdownMode) {
    if (const Markdown::Chapter* chapter = chapterForPage(currentPage)) {
      title = chapter->title;
    }
  }
  if (title.empty()) {
    title = txt->getTitle();
  }
}

void TxtReaderActivity::renderStatusBar() const {
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  std::string title;
  TextRole titleRole = TextRole::System;
  buildStatusBarTitle(title, titleRole);
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title, 0, 0, titleRole);
}

void TxtReaderActivity::saveProgress() const {
  FsFile f;
  if (Storage.openFileForWrite("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = 0;
    data[3] = 0;
    f.write(data, 4);
  }
}

void TxtReaderActivity::loadProgress() {
  FsFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) currentPage = totalPages - 1;
      if (currentPage < 0) currentPage = 0;
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }
  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) return false;
  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) return false;
  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) return false;
  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) return false;
  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) return false;
  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) return false;
  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) return false;
  uint8_t alignment;
  serialization::readPod(f, alignment);
  uint32_t numPages;
  serialization::readPod(f, numPages);
  const size_t cacheHeaderSize = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(int32_t) +
                                 sizeof(int32_t) + sizeof(int32_t) + sizeof(int32_t) + sizeof(uint8_t) +
                                 sizeof(uint32_t);
  if (numPages == 0 || cacheHeaderSize + static_cast<size_t>(numPages) * sizeof(uint32_t) > f.size()) {
    return false;
  }
  pageOffsets.clear();
  pageOffsets.reserve(numPages);
  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }
  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }
  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));
  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }
  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}
