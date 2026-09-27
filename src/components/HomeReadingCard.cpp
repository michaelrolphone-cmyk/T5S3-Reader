#include "HomeReadingCard.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "RecentBooksStore.h"
#include "ReadingCardStyle.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
using ReadingCardStyle::Box;

void drawMetadata(const GfxRenderer& renderer, const Box& area, const RecentBook* book) {
  if (area.width <= 0 || area.height <= 0) return;
  const int titleHeight = BaseTheme::getLineHeightForRole(renderer, UI_12_FONT_ID, TextRole::UserContent);
  const int authorHeight = BaseTheme::getLineHeightForRole(renderer, UI_10_FONT_ID, TextRole::UserContent);
  const int labelHeight = renderer.getLineHeight(UI_10_FONT_ID);
  if (titleHeight <= 0 || labelHeight <= 0) return;

  const char* label = tr(book ? STR_CONTINUE_READING : STR_NO_OPEN_BOOK);
  const std::string caption = renderer.truncatedText(UI_10_FONT_ID, label, area.width);
  const bool showCaption = !caption.empty() && renderer.getTextWidth(UI_10_FONT_ID, caption.c_str()) <= area.width &&
                           labelHeight <= area.height &&
                           (!book || titleHeight > area.height || labelHeight + 12 + titleHeight <= area.height);
  constexpr int gap = 12;
  std::string title;
  std::string author;
  if (book) {
    title = book->title;
    if (title.empty()) title = book->path.substr(book->path.find_last_of('/') + 1);
    if (!book->author.empty() && authorHeight > 0) {
      author = BaseTheme::truncatedTextForRole(renderer, UI_10_FONT_ID, TextRole::UserContent,
                                               book->author.c_str(), area.width);
      if (BaseTheme::getTextWidthForRole(renderer, UI_10_FONT_ID, TextRole::UserContent, author.c_str()) > area.width) {
        author.clear();
      }
    }
  }

  const int captionSpace = showCaption ? labelHeight + gap : 0;
  // In a shallow viewport, retain the title before the author.
  if (area.height - captionSpace < titleHeight + authorHeight + gap) author.clear();
  const int authorSpace = author.empty() ? 0 : authorHeight + gap;
  const int maxLines = std::clamp((area.height - captionSpace - authorSpace) / titleHeight, 0, 3);
  auto lines = BaseTheme::wrappedTextForRole(renderer, UI_12_FONT_ID, TextRole::UserContent,
                                            title.c_str(), area.width, maxLines);
  if (static_cast<int>(lines.size()) > maxLines) lines.resize(maxLines);
  const int blockHeight = (showCaption ? labelHeight : 0) +
                          (showCaption && !lines.empty() ? gap : 0) +
                          static_cast<int>(lines.size()) * titleHeight + authorSpace;
  int y = area.y + std::max(0, (area.height - blockHeight) / 2);
  if (showCaption) {
    renderer.drawText(UI_10_FONT_ID, area.x, y, caption.c_str(), false);
    y += labelHeight + (lines.empty() ? 0 : gap);
  }
  for (const auto& line : lines) {
    if (y + titleHeight <= area.y + area.height &&
        BaseTheme::getTextWidthForRole(renderer, UI_12_FONT_ID, TextRole::UserContent, line.c_str()) <= area.width) {
      BaseTheme::drawTextForRole(renderer, UI_12_FONT_ID, TextRole::UserContent, area.x, y, line.c_str(), false);
    }
    y += titleHeight;
  }
  if (!author.empty() && y + gap + authorHeight <= area.y + area.height) {
    BaseTheme::drawTextForRole(renderer, UI_10_FONT_ID, TextRole::UserContent, area.x, y + gap, author.c_str(), false);
  }
}
}  // namespace

void HomeReadingCard::draw(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (metrics.homeRecentBooksCount != 1) {
    GUI.drawRecentBookCover(renderer, rect, recentBooks, selectorIndex, coverRendered, coverBufferStored,
                            bufferRestored, storeCoverBuffer);
    return;
  }

  // Work only inside the existing Home slot. No geometry, input, menu or panel
  // state is changed, and off-screen/tiny slots cannot issue oversized draws.
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  if (screenWidth <= 0 || screenHeight <= 0 || rect.width <= 0 || rect.height <= 0) return;
  const int left = std::clamp(rect.x, 0, screenWidth);
  const int top = std::clamp(rect.y, 0, screenHeight);
  const int right = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(rect.x) + rect.width, 0, screenWidth));
  const int bottom = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(rect.y) + rect.height, 0, screenHeight));
  if (right <= left || bottom <= top) return;
  const Box bounds{left, top, right - left, bottom - top};
  auto layout = ReadingCardStyle::layout(bounds, metrics.contentSidePadding, metrics.homeCoverHeight, 0, 0);
  const RecentBook* book = recentBooks.empty() ? nullptr : &recentBooks.front();

  if (!(coverRendered && coverBufferStored && bufferRestored)) {
    const Box& card = layout.card;
    ReadingCardStyle::paint(card, layout.radius, [&renderer, &card](int x, int y, int light) {
      renderer.drawPixel(card.x + x, card.y + y, ReadingCardStyle::blackPixel(card.x + x, card.y + y, light));
    });

    // A failed open/parse always falls back to real title/author text. The art
    // stays uninverted, aspect-fitted, and entirely inside the dark frame.
    if (book && !book->coverBmpPath.empty()) {
      const std::string path = UITheme::getCoverThumbPath(book->coverBmpPath, metrics.homeCoverHeight);
      FsFile file;
      if (Storage.openFileForRead("HOME", path, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          layout = ReadingCardStyle::layout(bounds, metrics.contentSidePadding, metrics.homeCoverHeight,
                                             bitmap.getWidth(), bitmap.getHeight());
          const Box& cover = layout.cover;
          if (cover.width > 0 && cover.height > 0) {
            renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height);
            renderer.drawRect(cover.x - 1, cover.y - 1, cover.width + 2, cover.height + 2, false);
          }
        }
        file.close();
      }
    }
    drawMetadata(renderer, layout.text, book);

    // Store the unfocused card, including metadata and the no-cover fallback.
    // Cover generation invalidates coverRendered; failed cache allocation must
    // repaint next time instead of treating missing pixels as a valid cache.
    coverBufferStored = storeCoverBuffer && storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // Never invert the text or art on selection. Menu-resume themes keep their
  // existing menu focus; direct-card themes add a separate, uncached keyline.
  if (book && selectorIndex == 0 && !metrics.homeContinueReadingInMenu &&
      layout.card.width > 16 && layout.card.height > 16) {
    renderer.drawRoundedRect(layout.card.x + 5, layout.card.y + 5,
                              layout.card.width - 10, layout.card.height - 10, 1,
                              std::max(0, layout.radius - 5), false);
  }
}
