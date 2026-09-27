#include "HomeReadingCard.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "RecentBooksStore.h"
#include "ReadingCardStyle.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
using ReadingCardStyle::Box;

Box clippedBounds(const GfxRenderer& renderer, Rect rect) {
  return ReadingCardStyle::clip({rect.x, rect.y, rect.width, rect.height},
                                renderer.getScreenWidth(), renderer.getScreenHeight());
}

void drawMetadata(const GfxRenderer& renderer, const Box& area, const RecentBook* book) {
  if (area.width <= 0 || area.height <= 0) return;
  const int titleHeight = BaseTheme::getLineHeightForRole(renderer, UI_12_FONT_ID, TextRole::UserContent);
  const int authorHeight = BaseTheme::getLineHeightForRole(renderer, UI_10_FONT_ID, TextRole::UserContent);
  const int labelHeight = renderer.getLineHeight(UI_10_FONT_ID);
  if (titleHeight <= 0 || labelHeight <= 0) return;

  const char* label = book ? tr(STR_CONTINUE_READING) : tr(STR_NO_OPEN_BOOK);
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

void drawTonePlane(GfxRenderer& renderer, const ReadingCardStyle::Layout& layout, bool lsb) {
  // All non-card pixels must start with zero component bits. clearScreen only
  // clears RAM; neither this nor either copy issues a physical panel refresh.
  renderer.clearScreen(0x00);
  ReadingCardStyle::paint(layout.card, layout.radius, [&renderer, &layout, lsb](int x, int y, ReadingCardStyle::Tone tone) {
    if (ReadingCardStyle::componentBit(tone, lsb)) {
      renderer.drawPixel(layout.card.x + x, layout.card.y + y, false);  // Set plane bit to 1.
    }
  });
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

  const Box bounds = clippedBounds(renderer, rect);
  if (bounds.width <= 0 || bounds.height <= 0) return;
  auto layout = ReadingCardStyle::layout(bounds, metrics.contentSidePadding, metrics.homeCoverHeight, 0, 0);
  const RecentBook* book = recentBooks.empty() ? nullptr : &recentBooks.front();

  if (!(coverRendered && coverBufferStored && bufferRestored)) {
    const Box& card = layout.card;
    ReadingCardStyle::paint(card, layout.radius, [&renderer, &card](int x, int y, ReadingCardStyle::Tone tone) {
      // Both gray tones have black BASE pixels. present() supplies their actual
      // component planes; drawing an ordinary dithered Color here is incorrect.
      renderer.drawPixel(card.x + x, card.y + y, tone != ReadingCardStyle::White);
    });

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
            // Production BMP drawing leaves white pixels untouched.
            renderer.fillRect(cover.x, cover.y, cover.width, cover.height, false);
            renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height);
            renderer.drawRect(cover.x - 1, cover.y - 1, cover.width + 2, cover.height + 2, false);
          }
        }
        file.close();
      }
    }
    drawMetadata(renderer, layout.text, book);
    // This remains a BW base cache, not a cache of gray scratch planes. It
    // includes metadata/fallback but never selection or the menu below Home.
    coverBufferStored = storeCoverBuffer && storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // A short white focus mark does not obliterate the asymmetric reflective rim.
  // Menu-resume themes keep their menu focus. Cache restoration removes this.
  if (book && selectorIndex == 0 && !metrics.homeContinueReadingInMenu &&
      layout.card.width >= 64 && layout.card.height >= 32) {
    renderer.fillRect(layout.card.x + layout.card.width / 2 - 12,
                       layout.card.y + layout.card.height - 10, 24, 2, false);
  }
}

bool HomeReadingCard::present(GfxRenderer& renderer, Rect rect) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Box bounds = clippedBounds(renderer, rect);
  if (metrics.homeRecentBooksCount != 1 || bounds.width <= 0 || bounds.height <= 0) {
    renderer.displayBuffer();
    return false;
  }
  const auto layout = ReadingCardStyle::layout(bounds, metrics.contentSidePadding, metrics.homeCoverHeight, 0, 0);
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("HOME", "Reading card grayscale unavailable: BW snapshot allocation failed");
    renderer.displayBuffer();
    return false;
  }
  if (!renderer.captureGrayscaleBaseBuffer()) {
    renderer.restoreBwBuffer();
    LOG_ERR("HOME", "Reading card grayscale unavailable: base plane allocation failed");
    renderer.displayBuffer();
    return false;
  }

  drawTonePlane(renderer, layout, true);
  renderer.copyGrayscaleLsbBuffers();
  drawTonePlane(renderer, layout, false);
  renderer.copyGrayscaleMsbBuffers();
  // Restore the COMPLETE frame before either presentation path. The header,
  // cover, text, focus and menu survive both success and failed gray allocation.
  renderer.restoreBwBuffer();
  if (!renderer.grayscaleBuffersReady()) {
    LOG_ERR("HOME", "Reading card grayscale unavailable: component plane allocation failed");
    renderer.displayBuffer();
    return false;
  }
  // One physical presentation, using the existing four-gray compositor and
  // gray-capable refresh (not FAST_REFRESH, which would erase the gray tones).
  renderer.displayGrayBuffer(HalDisplay::HALF_REFRESH);
  return true;
}
