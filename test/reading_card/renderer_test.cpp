#include "TestApi.h"
#include "components/HomeReadingCard.h"
#include "components/ReadingCardStyle.h"
#include <climits>
#include <iostream>

using namespace ReadingCardStyle;
static void reset() { state = TestState{}; UITheme::getInstance().metrics = ThemeMetrics{}; }
static const std::vector<RecentBook> books{{"/Books/example.epub", "A real book title", "An Author", "cover.bmp"}};
static const Rect slot{0, 55, 540, 350};
static void draw(GfxRenderer& r, const std::vector<RecentBook>& b = books, int selection = 1) {
  bool painted = false, cached = false, restored = false;
  HomeReadingCard::draw(r, slot, b, selection, painted, cached, restored, [] { return false; });
}

int main() {
  for (bool flipped : {false, true}) {
    for (bool landscape : {false, true}) {
      reset();
      GfxRenderer r(landscape ? 960 : 540, landscape ? 540 : 960, flipped);
      draw(r);
      // Header and menu samples must survive all scratch clears and copies.
      r.fillRect(11, 12, 31, 7, true);
      r.fillRect(11, 470, 85, 13, true);
      const auto original = r.frame;
      assert(HomeReadingCard::present(r, slot));
      assert(r.frame == original && r.saved.empty() && r.presentations == 1 && r.grayPresentations == 1);
      assert((r.calls == std::vector<std::string>{"save", "base", "clear", "lsb", "clear", "msb", "restore", "gray"}));
      auto l = layout({0, 55, 540, 350}, 20, 300, 0, 0);
      int tones[4]{};
      paint(l.card, l.radius, [&](int x, int y, Tone tone) {
        const int px = l.card.x + x, py = l.card.y + y;
        ++tones[r.at(px, py)];
        if (x < 24 || x >= l.card.width - 24 || y < 24 || y >= l.card.height - 24) {
          const auto& art = r.bitmapRect;
          const bool artBorder = px >= art.x - 1 && px <= art.x + art.width &&
                                 py >= art.y - 1 && py <= art.y + art.height;
          if (!artBorder) assert(r.at(px, py) == tone); // Decoded pixels, not call counts.
        }
      });
      for (int count : tones) assert(count > 0);
      assert(r.at(11, 12) == Black && r.at(11, 470) == Black && r.at(0, 0) == White);
      // No chrome gray may leak into either the black or white cover pixels.
      for (int y = r.bitmapRect.y; y < r.bitmapRect.y + r.bitmapRect.height; ++y) {
        for (int x = r.bitmapRect.x; x < r.bitmapRect.x + r.bitmapRect.width; ++x) {
          assert(r.at(x, y) == (r.bit(original, x, y) ? White : Black));
        }
      }
      for (const auto& t : r.texts) {
        assert(!t.black && t.x > r.bitmapRect.x + r.bitmapRect.width);
        assert(r.at(t.x, t.y) == White);
      }
      const auto first = r.shown;
      assert(HomeReadingCard::present(r, slot));
      assert(r.shown == first && r.frame == original && r.presentations == 2);
    }
  }
  for (int failure = 1; failure <= 4; ++failure) {
    reset(); GfxRenderer r; draw(r); const auto original = r.frame;
    r.failure = failure;
    assert(!HomeReadingCard::present(r, slot));
    assert(r.frame == original && r.saved.empty() && r.presentations == 1 && r.grayPresentations == 0);
    for (int y = 0; y < r.height; ++y) for (int x = 0; x < r.width; ++x) {
      assert(r.at(x, y) == (r.bit(original, x, y) ? White : Black));
    }
    r.failure = 0;
    assert(HomeReadingCard::present(r, slot));
    assert(r.frame == original && r.saved.empty() && r.grayPresentations == 1);
  }
  reset();
  {
    GfxRenderer r;
    bool painted = false, cached = false, restored = false;
    std::vector<uint8_t> cache;
    int stores = 0;
    auto store = [&] { cache = r.frame; ++stores; return true; };
    HomeReadingCard::draw(r, slot, books, 0, painted, cached, restored, store);
    assert(painted && cached && stores == 1 && state.opens == 1);
    const auto focused = r.frame;
    assert(cache != focused); // Focus not baked into the base cache.
    assert(HomeReadingCard::present(r, slot));
    assert(r.frame == focused && state.opens == 1);
    r.frame = cache; restored = true;
    HomeReadingCard::draw(r, slot, books, 1, painted, cached, restored, store);
    assert(r.frame == cache && stores == 1 && state.opens == 1);
    assert(HomeReadingCard::present(r, slot));
    assert(r.frame == cache && state.opens == 1);
    painted = false; // New thumbnail invalidation, despite restored old cache.
    HomeReadingCard::draw(r, slot, books, 1, painted, cached, restored, store);
    assert(stores == 2 && state.opens == 2);
  }
  for (int failure = 0; failure < 4; ++failure) {
    reset(); GfxRenderer r; auto b = books;
    if (failure == 0) state.open = false;
    if (failure == 1) state.parse = false;
    if (failure == 2) state.width = 0;
    if (failure == 3) b[0].coverBmpPath.clear();
    draw(r, b);
    assert(r.bitmaps == 0 && r.texts.size() >= 3);
    assert(HomeReadingCard::present(r, slot));
    for (const auto& t : r.texts) assert(t.x == 44 && r.at(t.x, t.y) == White);
  }
  reset();
  {
    GfxRenderer r; draw(r, {});
    assert(r.bitmaps == 0 && state.opens == 0 && r.texts.size() == 1 && r.texts[0].value == "No open book");
    assert(HomeReadingCard::present(r, slot));
  }
  reset();
  {
    GfxRenderer r; bool a = false, b = false, c = false;
    UITheme::getInstance().metrics.homeRecentBooksCount = 3;
    HomeReadingCard::draw(r, slot, books, 0, a, b, c, [] { return true; });
    assert(state.delegates == 1);
    assert(!HomeReadingCard::present(r, slot) && r.presentations == 1 && r.calls == std::vector<std::string>{"bw"});
  }
  reset();
  for (Rect rect : {Rect{0, 0, 1, 1}, Rect{-20, -20, 50, 50}, Rect{0, 0, -5, 50},
                   Rect{INT_MAX, INT_MAX, 400, 400}, Rect{530, 950, INT_MAX, INT_MAX},
                   Rect{0, 55, 160, 100}, Rect{0, 55, 540, 80}}) {
    GfxRenderer r; bool a = false, b = false, c = false;
    HomeReadingCard::draw(r, rect, books, 0, a, b, c, [] { return false; });
    const auto original = r.frame;
    HomeReadingCard::present(r, rect);
    assert(r.frame == original && r.presentations == 1);
  }
  reset();
  {
    state.width = 96; state.height = 128;
    GfxRenderer r; draw(r);
    assert(r.bitmaps == 1 && r.bitmapRect.width == 96 && r.bitmapRect.height == 128);
  }
  reset();
  {
    GfxRenderer r; auto b = books;
    b[0].title.clear(); b[0].author.clear(); b[0].coverBmpPath.clear(); draw(r, b);
    assert(r.texts.back().value == "example.epub");
  }
  reset();
  {
    GfxRenderer r; auto b = books; b[0].title = std::string(500, 'W'); state.contentHeight = 100;
    draw(r, b); assert(!r.texts.empty());
    for (const auto& t : r.texts) assert(t.y + (t.value == "Continue reading" ? 20 : 100) <= 405);
  }
  std::cout << "PASS: decoded four-gray pixels, both orientations/flips, artwork/text protection, one presentation, "
               "full-frame restoration, all four allocation failures/recovery, cache and metadata cases\n";
}
