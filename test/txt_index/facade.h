#include <Markdown.h>
#include <utility>
namespace EpdFontFamily { enum Style { REGULAR, BOLD }; }
static unsigned metricDelay;
struct Renderer {
  bool sd = false;
  std::vector<std::string> prepared;
  bool isSdCardFont(int) { return sd; }
  void ensureSdCardFontReady(int, const char* text, int) { prepared.emplace_back(text); }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style) { card_time += metricDelay; return std::strlen(text) * 8; }
  int getLineHeight(int) { return 20; }
};
static unsigned taskYields;
void vTaskDelay(unsigned n) { ++taskYields; card_time += n; }
#include <Arduino.h>
struct TxtDisplayLine { std::string text; uint8_t headingLevel; };
struct Gui { void drawPopup(Renderer&, const char*) {} } GUI;
const char* tr(int) { return "indexing"; }
constexpr int STR_INDEXING = 1;
class TxtReaderActivity {
 public:
  Txt* txt;
  Renderer renderer;
  int cachedFontId = 0, viewportWidth = 400, viewportHeight = 640, linesPerPage = 32;
  bool markdownMode = false;
  std::vector<size_t> pageOffsets;
  std::vector<uint8_t> pageFenceOpen;
  int totalPages = 1;
  bool loadPageAtOffset(size_t, bool, std::vector<TxtDisplayLine>&, size_t&, bool&, Txt::ReadWindow* = nullptr);
  bool buildPageIndex();
};
