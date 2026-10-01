/**
 * XtcReaderActivity.h
 *
 * XTC ebook reader activity for CrossPoint Reader
 * Displays pre-rendered XTC pages on e-ink display
 */

#pragma once

#include <Xtc.h>

#include "activities/Activity.h"

class XtcReaderActivity final : public Activity {
  std::shared_ptr<Xtc> xtc;
  DisplayPresentMode initialRefreshMode = DisplayPresentMode::Clean;

  uint32_t currentPage = 0;
  int pagesUntilFullRefresh = 0;

  void openChapterSelection();
  void renderPage();
  void saveProgress() const;
  void loadProgress();
  void maybeAutoRemoveFromRecents() const;

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Xtc> xtc,
                             DisplayPresentMode initialRefreshMode = DisplayPresentMode::Clean)
      : Activity("XtcReader", renderer, mappedInput),
        xtc(std::move(xtc)),
        initialRefreshMode(initialRefreshMode) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};
