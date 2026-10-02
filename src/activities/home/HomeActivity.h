#pragma once
#include <T5AppApi.h>

#include <functional>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool appsPending = false;
  bool appsResume = false;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool coverRendered = false;
  bool coverBufferStored = false;
  uint8_t* coverBuffer = nullptr;
  std::vector<RecentBook> recentBooks;
  std::vector<t5_app_manifest_t> homeApps;
  std::string pendingHomeAppArtifact;
  std::string lastVisibleTextPrewarmKey;
#if defined(BOARD_XTEINK_X4_PRO)
  std::string x4Status;
#endif
  void onSelectBook(const std::string& path);
  void onRecentsOpen();
  void onSettingsOpen();
  void onOpdsBrowserOpen();
  void onHomeAppOpen(size_t index);
  void activateSelection(int index);

  int getMenuItemCount() const;
  bool storeCoverBuffer();
  bool restoreCoverBuffer();
  void freeCoverBuffer();
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);
  void loadHomeApps();
  bool needsRecentCovers(int coverHeight) const;

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
#if defined(BOARD_XTEINK_X4_PRO)
  X4NavigationResult onX4Navigation(uint32_t pressed, uint32_t released) override;
#endif
  bool onTouchTap(int16_t x, int16_t y) override;
  bool showsHomeTouchButton() const override { return false; }
  void render(RenderLock&&) override;
};
