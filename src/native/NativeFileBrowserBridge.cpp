#include <T5AppApi.h>
#include <T5FileBrowserApi.h>

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <NativeAppLauncher.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NativeAppHost.h"
#include "NativeSystemUiBridge.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;
constexpr char UTF8_ELLIPSIS[] = "\xE2\x80\xA6";

struct BrowserLayout {
  int rowTop = 0;
  int rowHeight = 0;
  int pageItems = 1;
  int pageStart = 0;
  int rowCount = 0;
};

struct ConfirmationState { bool available = false; bool confirmed = false; uint64_t cookie = 0; };
struct LaunchState { bool available = false; int32_t error = 0; uint64_t cookie = 0; };

BrowserLayout layout;
ConfirmationState confirmationState;
LaunchState launchState;
std::unique_ptr<ButtonNavigator> navigator;
bool confirmPressStarted = false;
unsigned long confirmPressedAt = 0;
bool lockNextConfirmRelease = false;
bool rootLongPressTriggered = false;

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }
GfxRenderer* gfx() { return active() ? &activityManager.nativeAppRenderer() : nullptr; }
MappedInputManager* input() { return active() ? &activityManager.nativeAppInput() : nullptr; }
bool validResumePath(const char* path) { return path && std::strncmp(path, "/sd/", 4) == 0 && path[4] != '\0'; }
bool validStoragePath(const char* path) { return path && path[0] == '/' && std::strncmp(path, "/sd/", 4) != 0; }
bool hasUnreadResult() { return confirmationState.available || launchState.available; }

Rect rotatePortraitRectToCurrentOrientation(const Rect& rect, const GfxRenderer& r) {
  const int portraitWidth = r.getDisplayVisibleWidth();
  const int portraitHeight = r.getDisplayVisibleHeight();
  switch (r.getOrientation()) {
    case GfxRenderer::Orientation::Portrait: return rect;
    case GfxRenderer::Orientation::LandscapeClockwise:
      return Rect(portraitHeight - rect.y - rect.height, rect.x, rect.height, rect.width);
    case GfxRenderer::Orientation::PortraitInverted:
      return Rect(portraitWidth - rect.x - rect.width, portraitHeight - rect.y - rect.height, rect.width, rect.height);
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      return Rect(rect.y, portraitWidth - rect.x - rect.width, rect.height, rect.width);
  }
  return rect;
}

bool containsPoint(const Rect& rect, int16_t x, int16_t y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

std::string entryKey(const t5_file_browser_entry_t& entry) {
  std::string value = entry.name ? entry.name : "";
  if (entry.is_directory) value += "/";
  return value;
}
std::string displayName(const t5_file_browser_entry_t& entry) {
  std::string value = entry.name ? entry.name : "";
  if (entry.is_directory) {
    if (!UITheme::getInstance().getTheme().showsFileIcons()) return "[" + value + "]";
    return value;
  }
  const auto pos = value.rfind('.');
  return pos == std::string::npos ? value : value.substr(0, pos);
}
std::string extension(const t5_file_browser_entry_t& entry) {
  if (entry.is_directory) return "";
  const std::string value = entry.name ? entry.name : "";
  const auto pos = value.rfind('.');
  return pos == std::string::npos ? "" : value.substr(pos);
}

void renderBrowser(const char* pathValue, const char* statusValue, const t5_file_browser_entry_t* entries,
                   uint32_t entryCount, int32_t selectedIndex) {
  auto* r = gfx();
  auto* in = input();
  if (!r || !in || (entryCount && !entries)) return;
  const std::string path = pathValue && pathValue[0] ? pathValue : "/";
  const std::string status = statusValue ? statusValue : "";
  const int selected = entryCount ? std::clamp(selectedIndex, 0, static_cast<int32_t>(entryCount) - 1) : 0;
  r->clearScreen();
  const auto pageWidth = r->getScreenWidth();
  const auto pageHeight = r->getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const std::string folderName = path == "/" ? std::string(tr(STR_SD_CARD)) : path.substr(path.rfind('/') + 1);
  const TextRole folderTitleRole = path == "/" ? TextRole::System : TextRole::UserContent;
  GUI.drawHeader(*r, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str(), nullptr,
                 folderTitleRole);
  const int pathLineHeight = BaseTheme::getLineHeightForRole(*r, SMALL_FONT_ID, TextRole::UserContent);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const int pageItems = std::max(1, contentHeight / std::max(1, metrics.listRowHeight));
  const int pageStart = entryCount ? (selected / pageItems) * pageItems : 0;
  if (entryCount == 0) {
    r->drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND));
  } else {
    GUI.drawList(*r, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entryCount), selected,
                 [entries](int i) { return displayName(entries[i]); }, nullptr,
                 [entries](int i) { return UITheme::getFileIcon(entryKey(entries[i])); },
                 [entries](int i) { return extension(entries[i]); }, false, TextRole::UserContent);
  }
  const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
  const int separatorY = pathY - metrics.verticalSpacing / 2;
  r->drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
  const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
  const char* pathStr = status.empty() ? path.c_str() : status.c_str();
  const char* pathDisplay = pathStr;
  char leftTruncBuf[256];
  if (BaseTheme::getTextWidthForRole(*r, SMALL_FONT_ID, TextRole::UserContent, pathStr) > pathMaxWidth) {
    const int ellipsisWidth = BaseTheme::getTextWidthForRole(*r, SMALL_FONT_ID, TextRole::UserContent, UTF8_ELLIPSIS);
    const int available = pathMaxWidth - ellipsisWidth;
    const char* p = pathStr;
    while (*p) {
      if (BaseTheme::getTextWidthForRole(*r, SMALL_FONT_ID, TextRole::UserContent, p) <= available) break;
      ++p;
      while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
    }
    snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", UTF8_ELLIPSIS, p);
    pathDisplay = leftTruncBuf;
  }
  BaseTheme::drawTextForRole(*r, SMALL_FONT_ID, TextRole::UserContent, metrics.contentSidePadding, pathY, pathDisplay);
  const char* backLabel = path == "/" ? tr(STR_HOME) : tr(STR_BACK);
  const char* confirmLabel = entryCount ? tr(STR_OPEN) : "";
  const auto labels = in->mapLabels(backLabel, confirmLabel, entryCount ? tr(STR_DIR_UP) : "",
                                    entryCount ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(*r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  layout = {contentTop, metrics.listRowHeight, pageItems, pageStart, static_cast<int>(entryCount)};
  r->displayBuffer(HalDisplay::BALANCED_REFRESH);
}

uint8_t buttonEvent(MappedInputManager::Button button) {
  using Button = MappedInputManager::Button;
  switch (button) {
    case Button::Back: return T5_FILE_BROWSER_EVENT_BACK;
    case Button::Confirm: return T5_FILE_BROWSER_EVENT_OPEN;
    case Button::Left:
    case Button::Up: return T5_FILE_BROWSER_EVENT_PREVIOUS;
    case Button::Right:
    case Button::Down: return T5_FILE_BROWSER_EVENT_NEXT;
    default: return T5_FILE_BROWSER_EVENT_NONE;
  }
}

bool pollBrowserEvent(t5_file_browser_event_t* event, uint32_t waitMs, bool atRoot, bool selectedIsDirectory) {
  if (!event) return false;
  *event = {};
  event->row_index = -1;
  const auto* core = t5_app_get_api(T5_APP_ABI_VERSION);
  auto* r = gfx();
  auto* in = input();
  if (!core || !core->poll || !core->millis || !r || !in) return false;
  t5_app_input_t raw{};
  if (!core->poll(&raw, waitMs)) return false;
  if (raw.exit_requested) { event->type = T5_FILE_BROWSER_EVENT_EXIT; return true; }
  MappedInputManager::TouchPoint swipeStart{}, swipeEnd{};
  if (in->getTouchSwipe(swipeStart, swipeEnd, *r)) {
    const int vertical = static_cast<int>(swipeEnd.y) - swipeStart.y;
    if (std::abs(vertical) >= 70 && layout.rowCount > 0) {
      event->type = vertical < 0 ? T5_FILE_BROWSER_EVENT_PAGE_NEXT : T5_FILE_BROWSER_EVENT_PAGE_PREVIOUS;
      return true;
    }
  }
  if (raw.tapped) {
    const auto bounds = GUI.getButtonHintTouchBounds(*r);
    for (size_t i = 0; i < bounds.size(); ++i) {
      const Rect oriented = rotatePortraitRectToCurrentOrientation(bounds[i], *r);
      if (!containsPoint(oriented, raw.touch_x, raw.touch_y)) continue;
      MappedInputManager::Button button;
      if (in->resolveTouchFrontButton(i, button)) { event->type = buttonEvent(button); return true; }
    }
    if (layout.rowHeight > 0 && raw.touch_y >= layout.rowTop) {
      const int local = (raw.touch_y - layout.rowTop) / layout.rowHeight;
      if (local >= 0 && local < layout.pageItems) {
        const int index = layout.pageStart + local;
        if (index >= 0 && index < layout.rowCount) {
          event->type = T5_FILE_BROWSER_EVENT_ROW;
          event->row_index = index;
          return true;
        }
      }
    }
  }
  using Button = MappedInputManager::Button;
  if (!atRoot && in->isPressed(Button::Back) && in->getHeldTime() >= LONG_PRESS_MS && !rootLongPressTriggered) {
    rootLongPressTriggered = true; event->type = T5_FILE_BROWSER_EVENT_ROOT; return true;
  }
  if (in->wasReleased(Button::Back)) {
    if (rootLongPressTriggered) { rootLongPressTriggered = false; return true; }
    if (in->getHeldTime() >= LONG_PRESS_MS) return true;
    event->type = T5_FILE_BROWSER_EVENT_BACK; return true;
  }
  if (in->wasPressed(Button::Confirm)) { confirmPressedAt = core->millis(); confirmPressStarted = true; }
  if (in->wasReleased(Button::Confirm)) {
    const unsigned long held = confirmPressStarted ? core->millis() - confirmPressedAt : in->getHeldTime();
    confirmPressStarted = false;
    if (lockNextConfirmRelease) { lockNextConfirmRelease = false; return true; }
    event->type = held >= LONG_PRESS_MS && !selectedIsDirectory ? T5_FILE_BROWSER_EVENT_DELETE
                                                                : T5_FILE_BROWSER_EVENT_OPEN;
    return true;
  }
  if (!navigator) navigator = std::make_unique<ButtonNavigator>();
  uint8_t nav = T5_FILE_BROWSER_EVENT_NONE;
  navigator->onNextRelease([&nav] { nav = T5_FILE_BROWSER_EVENT_NEXT; });
  if (nav == T5_FILE_BROWSER_EVENT_NONE) navigator->onPreviousRelease([&nav] { nav = T5_FILE_BROWSER_EVENT_PREVIOUS; });
  if (nav == T5_FILE_BROWSER_EVENT_NONE) navigator->onNextContinuous([&nav] { nav = T5_FILE_BROWSER_EVENT_PAGE_NEXT; });
  if (nav == T5_FILE_BROWSER_EVENT_NONE) navigator->onPreviousContinuous([&nav] { nav = T5_FILE_BROWSER_EVENT_PAGE_PREVIOUS; });
  event->type = nav;
  return true;
}

uint32_t pageItems() { return static_cast<uint32_t>(std::max(1, layout.pageItems)); }

class NativeDeleteConfirmationActivity final : public Activity {
  std::string resumePath;
  std::string entryName;
  uint64_t cookie;
  bool started = false;
  bool childCompleted = false;
  bool resumeReturned = false;
 public:
  NativeDeleteConfirmationActivity(GfxRenderer& gfxRenderer, MappedInputManager& mappedInput, std::string resume,
                                   std::string entry, uint64_t requestCookie)
      : Activity("NativeFileDelete", gfxRenderer, mappedInput), resumePath(std::move(resume)),
        entryName(std::move(entry)), cookie(requestCookie) {}
  void onEnter() override {
    Activity::onEnter();
    if (started) return;
    started = true;
    std::string heading = tr(STR_DELETE) + std::string("? ");
    startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entryName),
                           [this](const ActivityResult& result) {
                             confirmationState.available = true;
                             confirmationState.confirmed = !result.isCancelled;
                             confirmationState.cookie = cookie;
                             childCompleted = true;
                           });
  }
  void loop() override {
    if (resumeReturned) { finish(); return; }
    if (!childCompleted) return;
    childCompleted = false;
    if (resumePath.empty() || runNativeApp(resumePath.c_str(), renderer, mappedInput) != ESP_OK) {
      confirmationState = {}; finish(); return;
    }
    resumeReturned = true;
  }
  void render(RenderLock&&) override {}
};

class NativeChildElfActivity final : public Activity {
  std::string resumePath;
  std::string childPath;
  uint64_t cookie;
  bool ranChild = false;
  bool resumeReturned = false;
 public:
  NativeChildElfActivity(GfxRenderer& gfxRenderer, MappedInputManager& mappedInput, std::string resume,
                         std::string child, uint64_t requestCookie)
      : Activity("NativeFileElf", gfxRenderer, mappedInput), resumePath(std::move(resume)),
        childPath(std::move(child)), cookie(requestCookie) {}
  void loop() override {
    if (resumeReturned) { finish(); return; }
    if (!ranChild) {
      ranChild = true;
      launchState.available = true;
      launchState.error = runNativeApp(childPath.c_str(), renderer, mappedInput);
      launchState.cookie = cookie;
    }
    if (resumePath.empty() || runNativeApp(resumePath.c_str(), renderer, mappedInput) != ESP_OK) {
      launchState = {}; finish(); return;
    }
    resumeReturned = true;
  }
  void render(RenderLock&&) override {}
};

bool showHiddenFiles() { return SETTINGS.showHiddenFiles; }
bool confirmDeleteRequest(const char* entryName, uint64_t cookie) {
  const char* currentPath = native_app_current_path();
  if (!validResumePath(currentPath) || !entryName || !entryName[0] || hasUnreadResult()) return false;
  activityManager.pushActivity(std::make_unique<NativeDeleteConfirmationActivity>(
      renderer, mappedInputManager, std::string(currentPath), std::string(entryName), cookie));
  nativeSystemUiMarkActivityPending();
  return true;
}
bool confirmDeleteTakeResult(bool* confirmed, uint64_t* cookie) {
  if (!confirmationState.available) return false;
  if (confirmed) *confirmed = confirmationState.confirmed;
  if (cookie) *cookie = confirmationState.cookie;
  confirmationState = {};
  return true;
}
bool deleteDocument(const char* path) {
  if (!validStoragePath(path)) return false;
  const std::string_view documentPath(path);
  if (FsHelpers::hasEpubExtension(documentPath)) Epub(std::string(path), "/.crosspoint").clearCache();
  return Storage.remove(path);
}
bool openDocument(const char* path) {
  if (!validStoragePath(path) || hasUnreadResult()) return false;
  activityManager.goToReader(std::string(path));
  return true;
}
bool launchElfRequest(const char* sdVfsPath, uint64_t cookie) {
  const char* currentPath = native_app_current_path();
  if (!validResumePath(currentPath) || !validResumePath(sdVfsPath) || hasUnreadResult()) return false;
  if (!FsHelpers::checkFileExtension(std::string_view(sdVfsPath), ".elf")) return false;
  activityManager.pushActivity(std::make_unique<NativeChildElfActivity>(
      renderer, mappedInputManager, std::string(currentPath), std::string(sdVfsPath), cookie));
  nativeSystemUiMarkActivityPending();
  return true;
}
bool launchElfTakeResult(int32_t* espError, uint64_t* cookie) {
  if (!launchState.available) return false;
  if (espError) *espError = launchState.error;
  if (cookie) *cookie = launchState.cookie;
  launchState = {};
  return true;
}

const t5_file_browser_api_v1 api = {
    T5_FILE_BROWSER_API_VERSION, sizeof(t5_file_browser_api_v1), showHiddenFiles, renderBrowser,
    pollBrowserEvent, pageItems, confirmDeleteRequest, confirmDeleteTakeResult, deleteDocument,
    openDocument, launchElfRequest, launchElfTakeResult,
};
}  // namespace

extern "C" const t5_file_browser_api_v1* t5_file_browser_get_api(uint32_t version) {
  if (version != T5_FILE_BROWSER_API_VERSION || !active()) return nullptr;
  auto& in = activityManager.nativeAppInput();
  ButtonNavigator::setMappedInputManager(in);
  navigator = std::make_unique<ButtonNavigator>();
  layout = {};
  confirmPressStarted = false;
  rootLongPressTriggered = false;
  lockNextConfirmRelease = in.isPressed(MappedInputManager::Button::Confirm);
  return &api;
}
