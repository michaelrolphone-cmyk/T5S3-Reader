#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "native/NativeAppHost.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char UTF8_ELLIPSIS[] = "\xE2\x80\xA6";

void appendTextKey(std::string& key, const std::string& text) {
  if (text.empty()) {
    return;
  }
  key.push_back('\n');
  key += text;
}

void recordUserContentText(FontCacheManager* fcm, const int systemFontId, const char* text,
                           const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (fcm == nullptr || text == nullptr || text[0] == '\0') {
    return;
  }
  fcm->recordText(text, BaseTheme::resolveTextFontId(systemFontId, TextRole::UserContent), style);
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 6;  // File Browser, Recents, File transfer, Timecard, Apps, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    if (recentBooks.size() >= maxBooks) {
      break;
    }
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
  }
}

bool HomeActivity::needsRecentCovers(int coverHeight) const {
  for (const RecentBook& book : recentBooks) {
    if (book.coverBmpPath.empty()) {
      continue;
    }
    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    if (Storage.exists(coverPath.c_str())) {
      continue;
    }
    if (FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path)) {
      return true;
    }
  }
  return false;
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          epub.load(false, true);
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();
  hasOpdsServers = OPDS_STORE.hasServers();
  selectorIndex = 0;
  recentsLoading = false;
  firstRenderDone = false;
  coverRendered = false;
  coverBufferStored = false;
  lastVisibleTextPrewarmKey.clear();
  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  recentsLoaded = !needsRecentCovers(metrics.homeCoverHeight);
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();
  freeCoverBuffer();
  lastVisibleTextPrewarmKey.clear();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }
  freeCoverBuffer();
  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }
  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }
  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  if (appsPending) {
    appsPending = false;
    appsPending = runNativeSpringboard(renderer, mappedInput, appsResume);
    appsResume = appsPending;
    requestUpdate();
    return;
  }
  if (firstRenderDone && !recentsLoaded && !recentsLoading) {
    loadRecentCovers(UITheme::getInstance().getMetrics().homeCoverHeight);
    requestUpdate();
    return;
  }

  const int menuCount = getMenuItemCount();
  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection(selectorIndex);
  }
}

bool HomeActivity::onTouchTap(int16_t, int16_t y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (!metrics.homeContinueReadingInMenu && !recentBooks.empty() && y >= metrics.homeTopPadding &&
      y < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    selectorIndex = 0;
    onSelectBook(recentBooks[0].path);
    return true;
  }

  const Rect menuRect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
                      pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight +
                                    metrics.homeMenuTopOffset + metrics.buttonHintsHeight)};
  const int menuCount = getMenuItemCount() - (metrics.homeContinueReadingInMenu ? 0 : recentBooks.size());
  if (menuCount <= 0) return false;

  const int selected = metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  const auto layout = GUI.buttonMenuLayout(renderer, menuRect, selected);
  for (int i = layout.start; i < menuCount && i < layout.start + layout.pageSize; ++i) {
    const int rowY = layout.top + (i - layout.start) * layout.rowStep;
    if (y >= rowY && y < rowY + layout.rowHeight) {
      selectorIndex = metrics.homeContinueReadingInMenu ? i : static_cast<int>(recentBooks.size()) + i;
      activateSelection(selectorIndex);
      return true;
    }
  }
  return false;
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  std::string visibleTextKey;
  if (!recentBooks.empty()) {
    appendTextKey(visibleTextKey, recentBooks[0].title);
    appendTextKey(visibleTextKey, recentBooks[0].author);
  }

  if (auto* fcm = renderer.getFontCacheManager();
      fcm != nullptr && visibleTextKey != lastVisibleTextPrewarmKey) {
    fcm->resetRecordedText();
    if (!recentBooks.empty()) {
      recordUserContentText(fcm, UI_12_FONT_ID, recentBooks[0].title.c_str());
      recordUserContentText(fcm, UI_10_FONT_ID, recentBooks[0].author.c_str());
      recordUserContentText(fcm, UI_12_FONT_ID, UTF8_ELLIPSIS);
      recordUserContentText(fcm, UI_10_FONT_ID, UTF8_ELLIPSIS);
      if (metrics.homeContinueReadingInMenu) {
        recordUserContentText(fcm, UI_12_FONT_ID, recentBooks[0].title.c_str(), EpdFontFamily::BOLD);
        recordUserContentText(fcm, UI_12_FONT_ID, UTF8_ELLIPSIS, EpdFontFamily::BOLD);
      }
    }
    fcm->prewarmRecordedText();
    lastVisibleTextPrewarmKey = visibleTextKey;
  }

  char homeClockLabel[ClockFormat::BUFFER_SIZE] = {};
  const char* headerClockLabel = nullptr;
  if (!SETTINGS.hideClock && halClock.isAvailable() &&
      halClock.formatTime(homeClockLabel, sizeof(homeClockLabel), SETTINGS.timeFormat == CrossPointSettings::TIME_12H)) {
    headerClockLabel = homeClockLabel;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr,
                 nullptr, TextRole::UserContent, TextRole::System, headerClockLabel);

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_TIMECARD), tr(STR_APPS), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Clock, Library, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }

  if (metrics.homeContinueReadingInMenu) {
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  if (!firstRenderDone) {
    firstRenderDone = true;
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::activateSelection(int index) {
  int idx = 0;
  int menuSelectedIndex = index - static_cast<int>(recentBooks.size());
  const int fileBrowserIdx = idx++;
  const int recentsIdx = idx++;
  const int opdsLibraryIdx = hasOpdsServers ? idx++ : -1;
  const int fileTransferIdx = idx++;
  const int timecardIdx = idx++;
  const int appsIdx = idx++;
  const int settingsIdx = idx;

  if (index < static_cast<int>(recentBooks.size())) {
    onSelectBook(recentBooks[index].path);
  } else if (menuSelectedIndex == fileBrowserIdx) {
    onFileBrowserOpen();
  } else if (menuSelectedIndex == recentsIdx) {
    onRecentsOpen();
  } else if (menuSelectedIndex == opdsLibraryIdx) {
    onOpdsBrowserOpen();
  } else if (menuSelectedIndex == fileTransferIdx) {
    onFileTransferOpen();
  } else if (menuSelectedIndex == timecardIdx) {
    onTimecardOpen();
  } else if (menuSelectedIndex == appsIdx) {
    appsPending = true;
  } else if (menuSelectedIndex == settingsIdx) {
    onSettingsOpen();
  }
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onTimecardOpen() { activityManager.goToTimecard(); }
