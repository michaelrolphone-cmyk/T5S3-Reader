#pragma once

#include <EpdFontFamily.h>
#include <Markdown.h>
#include <Txt.h>

#include <cstdint>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"

enum class TextRole : int;

struct TxtDisplayLine {
  std::string text;
  uint8_t headingLevel = 0;
};

class TxtReaderActivity final : public Activity {
  std::unique_ptr<Txt> txt;
  HalDisplay::RefreshMode initialRefreshMode = HalDisplay::FULL_REFRESH;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  std::vector<size_t> pageOffsets;
  std::vector<uint8_t> pageFenceOpen;
  std::vector<TxtDisplayLine> currentPageLines;
  std::vector<Markdown::Chapter> chapters;
  int linesPerPage = 0;
  int viewportWidth = 0;
  int viewportHeight = 0;
  bool initialized = false;
  bool markdownMode = false;

  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage();
  void buildStatusBarTitle(std::string& title, TextRole& titleRole) const;
  void renderStatusBar() const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, bool fenceOpen, std::vector<TxtDisplayLine>& outLines, size_t& nextOffset,
                        bool& fenceOpenAfter);
  void buildPageIndex();
  void collectChapters();
  bool peekMarkdownLine(size_t offset, Markdown::Line& outLine) const;
  const Markdown::Chapter* chapterForPage(int page) const;
  void openChapterList();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();
  void finishReadingAndGoHome();
  int lineHeightFor(const TxtDisplayLine& line) const;
  EpdFontFamily::Style styleFor(const TxtDisplayLine& line) const;

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt,
                             HalDisplay::RefreshMode initialRefreshMode = HalDisplay::FULL_REFRESH)
      : Activity("TxtReader", renderer, mappedInput),
        txt(std::move(txt)),
        initialRefreshMode(initialRefreshMode) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};
