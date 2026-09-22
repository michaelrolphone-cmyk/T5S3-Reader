#include "HomeActivity.h"

#include <AppManifestRules.h>
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
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "native/AppManifest.h"
#include "native/InstalledAppPath.h"
#include "native/NativeAppHost.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char UTF8_ELLIPSIS[] = "\xE2\x80\xA6";
constexpr char HOME_APPS_PATH[] = "/Apps/.home_apps";

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
  int count = 5 + static_cast<int>(homeApps.size());  // File Browser, Recents, File transfer, Apps, pinned apps, Settings
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

void HomeActivity::loadHomeApps() {
  homeApps.clear();
  if (!Storage.ready() || !Storage.exists(HOME_APPS_PATH)) {
    return;
  }

  // Springboard persists ELF basenames, not package IDs or absolute paths.
  // Resolve each pin against the verified installed package inventory so
  // upgrades and canonical /Apps/<id>/<artifact> installs remain visible.
  HalFile pinFile = Storage.open(HOME_APPS_PATH, O_RDONLY);
  if (!pinFile.isOpen() || pinFile.isDirectory() || pinFile.fileSize64() > 16384u) return;
  pinFile.close();
  const String pins = Storage.readFile(HOME_APPS_PATH);
  const char* data = pins.c_str();
  const size_t length = pins.length();
  size_t start = 0;

  while (start < length && homeApps.size() < 128) {
    size_t end = start;
    while (end < length && data[end] != '\n' && data[end] != '\r') {
      ++end;
    }

    if (end > start) {
      const std::string fileName(data + start, end - start);
      if (t5_safe_elf_name(fileName.c_str()) && fileName != "springboard.elf") {
        std::string resolvedPath;
        t5_app_manifest_t manifest{};
        if (resolveInstalledAppPath(fileName.c_str(), resolvedPath, &manifest)) {
          bool duplicate = false;
          for (const auto& existing : homeApps) {
            if (!std::strcmp(existing.file_name, manifest.file_name)) {
              duplicate = true;
              break;
            }
          }
          if (!duplicate) {
            homeApps.push_back(manifest);
          }
        }
      }
    }

    while (end < length && (data[end] == '\n' || data[end] == '\r')) {
      ++end;
    }
    start = end;
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
  pendingHomeAppArtifact.clear();
  lastVisibleTextPrewarmKey.clear();
  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  loadHomeApps();
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
  if (!pendingHomeAppArtifact.empty()) {
    // Home selections run inside ActivityManager's current touch/button
    // dispatch. Defer the ELF until the next owner-task iteration so input
    // cleanup and UI dispatch fully finish first, matching Springboard and
    // File Browser child-app handoff.
    const std::string artifact = pendingHomeAppArtifact;
    pendingHomeAppArtifact.clear();
    std::string resolvedPath;
    if (resolveInstalledAppPath(artifact.c_str(), resolvedPath)) {
      runNativeApp(resolvedPath.c_str(), renderer, mappedInput);
    }
    loadHomeApps();
    requestUpdate();
    return;
  }
  if (appsPending) {
    appsPending = false;
    appsPending = runNativeSpringboard(renderer, mappedInput, appsResume);
    appsResume = appsPending;
    loadHomeApps();
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

  std::vector<const char*> menuItems;
  std::vector<UIIcon> menuIcons;
  menuItems.reserve(5 + homeApps.size() + (hasOpdsServers ? 1 : 0) + (metrics.homeContinueReadingInMenu ? 1 : 0));
  menuIcons.reserve(menuItems.capacity());

  menuItems.push_back(tr(STR_BROWSE_FILES));
  menuIcons.push_back(Folder);
  menuItems.push_back(tr(STR_MENU_RECENT_BOOKS));
  menuIcons.push_back(Recent);
  if (hasOpdsServers) {
    menuItems.push_back(tr(STR_OPDS_BROWSER));
    menuIcons.push_back(Library);
  }
  menuItems.push_back(tr(STR_FILE_TRANSFER));
  menuIcons.push_back(Transfer);
  menuItems.push_back(tr(STR_APPS));
  menuIcons.push_back(Library);
  for (const auto& app : homeApps) {
    menuItems.push_back(app.display_name);
    menuIcons.push_back(Library);
  }
  menuItems.push_back(tr(STR_SETTINGS_TITLE));
  menuIcons.push_back(Settings);

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
  const int appsIdx = idx++;
  const int homeAppsStartIdx = idx;
  idx += static_cast<int>(homeApps.size());
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
  } else if (menuSelectedIndex == appsIdx) {
    appsPending = true;
  } else if (menuSelectedIndex >= homeAppsStartIdx && menuSelectedIndex < settingsIdx) {
    onHomeAppOpen(static_cast<size_t>(menuSelectedIndex - homeAppsStartIdx));
  } else if (menuSelectedIndex == settingsIdx) {
    onSettingsOpen();
  }
}

void HomeActivity::onHomeAppOpen(size_t index) {
  if (index >= homeApps.size()) {
    return;
  }
  // Queue the basename, then resolve it again immediately before the deferred
  // launch. This preserves upgrade/uninstall revalidation without entering an
  // ELF from inside the current Home input-dispatch stack.
  pendingHomeAppArtifact = homeApps[index].file_name;
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
