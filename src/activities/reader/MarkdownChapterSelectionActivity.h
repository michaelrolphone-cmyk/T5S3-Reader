#pragma once

#include <Markdown.h>

#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class MarkdownChapterSelectionActivity final : public Activity {
  std::vector<Markdown::Chapter> chapters;
  ButtonNavigator buttonNavigator;
  int currentPage = 0;
  int selectorIndex = 0;

  int getPageItems() const;
  int findChapterIndexForPage(int page) const;

 public:
  MarkdownChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   std::vector<Markdown::Chapter> chapters, int currentPage)
      : Activity("MarkdownChapterSelection", renderer, mappedInput),
        chapters(std::move(chapters)),
        currentPage(currentPage) {}
  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
