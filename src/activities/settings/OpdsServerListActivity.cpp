#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/ActivityManager.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "native/NativeAppHost.h"

int OpdsServerListActivity::getItemCount() const {
  int count = static_cast<int>(OPDS_STORE.getCount());
  if (!pickerMode) count++;
  return count;
}

void OpdsServerListActivity::onEnter() {
  Activity::onEnter();
  launchAttempted = false;
  launchFailed = false;

  if (pickerMode) {
    OPDS_STORE.loadFromFile();
    selectedIndex = 0;
  }
  requestUpdate();
}

void OpdsServerListActivity::onExit() { Activity::onExit(); }

void OpdsServerListActivity::loop() {
  if (!pickerMode) {
    if (!launchAttempted) {
      launchAttempted = true;
      const esp_err_t result = runNativeApp("/sd/Apps/opds_settings.elf", renderer, mappedInput);
      if (result == ESP_OK) {
        // The native OPDS editor can queue a firmware keyboard activity.
        // Let ActivityManager push that child before this launcher pops.
        return;
      }
      launchFailed = true;
      requestUpdate();
      return;
    }

    if (!launchFailed) {
      finish();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int itemCount = getItemCount();
  if (itemCount > 0) {
    buttonNavigator.onNext([this, itemCount] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, itemCount] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
      requestUpdate();
    });
  }
}

void OpdsServerListActivity::handleSelection() {
  if (!pickerMode) return;
  const auto serverCount = static_cast<int>(OPDS_STORE.getCount());
  if (selectedIndex < serverCount) {
    const auto* server = OPDS_STORE.getServer(static_cast<size_t>(selectedIndex));
    if (server) {
      activityManager.replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, *server));
    }
  }
}

bool OpdsServerListActivity::onTouchTap(int16_t x, int16_t y) {
  if (!pickerMode) {
    if (!launchFailed) return true;
    MappedInputManager::Button button = MappedInputManager::Button::Back;
    if (!resolveTouchButtonHint(x, y, button)) return false;
    if (button == MappedInputManager::Button::Back || button == MappedInputManager::Button::Confirm) finish();
    return true;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int rowHeight = metrics.listWithSubtitleRowHeight;
  const int itemCount = getItemCount();
  if (itemCount <= 0 || rowHeight <= 0 || y < contentTop || y >= contentTop + contentHeight) return false;

  const int pageItems = std::max(1, contentHeight / rowHeight);
  const int row = (y - contentTop) / rowHeight;
  const int pageStartIndex = (selectedIndex / pageItems) * pageItems;
  const int touchedIndex = pageStartIndex + row;
  if (row < 0 || row >= pageItems || touchedIndex < 0 || touchedIndex >= itemCount) return false;

  selectedIndex = touchedIndex;
  handleSelection();
  return true;
}

void OpdsServerListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (!pickerMode) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "OPDS Servers");
    const int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
    if (launchFailed) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, "opds_settings.elf could not be launched");
      renderer.drawCenteredText(SMALL_FONT_ID, y + 36, "Install OPDS Servers from the App Store or copy it to /Apps.");
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "OK", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Opening OPDS Servers...");
    }
    renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
    return;
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_OPDS_SERVERS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int itemCount = getItemCount();

  if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_SERVERS));
  } else {
    const auto& servers = OPDS_STORE.getServers();
    const auto serverCount = static_cast<int>(servers.size());
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
        [&servers, serverCount](int index) {
          if (index < serverCount) {
            const auto& server = servers[index];
            return server.name.empty() ? server.url : server.name;
          }
          return std::string(I18n::getInstance().get(StrId::STR_ADD_SERVER));
        },
        [&servers, serverCount](int index) {
          if (index < serverCount && !servers[index].name.empty()) return servers[index].url;
          return std::string("");
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
