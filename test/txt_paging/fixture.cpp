#include "fixture.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include <builtinFonts/notosans_12_regular.h>
#include <builtinFonts/notosans_12_bold.h>
#pragma GCC diagnostic pop
#include "paging.inc"

static std::string repeated(const std::string& seed, size_t bytes) {
  std::string out;
  while (out.size() < bytes) out += seed;
  out.resize(bytes);
  return out;
}

static size_t cases = 0;
static Work run(TxtReaderActivity& reader, const std::string& label, size_t offset = 0,
                bool fence = false) {
  work = {};
  // Stale outputs must be cleared even when the operation fails.
  std::vector<TxtDisplayLine> lines{{"stale", 6}};
  size_t next = 987654;
  bool after = !fence;
  std::cout << "case " << ++cases << ' ' << label << ' ' << offset << ' ' << fence << '\n';
  const bool ok = reader.loadPageAtOffset(offset, fence, lines, next, after);
  std::cout << "result " << ok << ' ' << next << ' ' << after << ' ' << lines.size() << '\n';
  for (const auto& line : lines) {
    std::cout << "line " << unsigned(line.headingLevel) << '\n';
    recordText(line.text);
  }
  std::cout << "effects " << work.measures << ' ' << work.measuredBytes << ' ' << work.reads
            << ' ' << work.allocations << ' ' << work.releases << ' ' << work.yields
            << ' ' << work.prepared << '\n';
  assert(work.releases == work.allocations - (failAllocation ? work.allocations : 0));
  assert(after == fence || reader.markdownMode);
  if (offset >= reader.txt->content.size() || failAllocation || reader.txt->failRead) {
    assert(!ok && lines.empty() && next == 987654 && after == fence);
  }
  return work;
}

int main() {
  const EpdFont regular(&notosans_12_regular), bold(&notosans_12_bold);
  GfxRenderer renderer;
  renderer.fontMap.emplace(1, EpdFontFamily(&regular, &bold));
  Txt text;
  TxtReaderActivity reader{&text, renderer};
  text.content = "short";
  const auto fitting = run(reader, "fitting-line");
  assert(fitting.measures == 1 && fitting.boundaries == 0 && fitting.boundaryTables == 0);
  const std::string prose = "Ordinary paragraphs keep the same word breaks, font measurements and source offsets. ";
  for (bool md : {false, true}) {
    reader.markdownMode = md;
    for (int width : {400, 540}) {
      reader.viewportWidth = width;
      for (size_t size : {size_t(1024), size_t(4096), size_t(8192)}) {
        text.content = repeated(prose, size);
        const auto cost = run(reader, "prose-" + std::to_string(size));
        std::cerr << "prose bytes=" << size << " markdown=" << md << " width=" << width
                  << " boundaries=" << cost.boundaries << " boundary_bytes=" << cost.boundaryBytes
                  << " tables=" << cost.boundaryTables << " growths=" << cost.boundaryGrowths
                  << " measures=" << cost.measures << " measured_bytes=" << cost.measuredBytes << '\n';
#ifdef ENFORCE_COST
        assert(cost.boundaryTables == 1);
        assert(cost.boundaries <= size && cost.boundaryBytes <= size);
#endif
      }
    }
  }
  // Independent page calls and a complete long-document walk cannot reuse
  // stale paragraph state. The original offset/fence behavior is recorded.
  reader.viewportWidth = 400;
  for (bool md : {false, true}) {
    reader.markdownMode = md;
    text.content = repeated(prose, 65536);
    size_t offset = 0, pages = 0;
    bool fence = false;
    size_t totalBoundaries = 0, totalBoundaryBytes = 0;
    while (offset < text.content.size()) {
      work = {};
      std::vector<TxtDisplayLine> lines;
      size_t next = offset;
      bool after = fence;
      std::cout << "walk " << md << ' ' << pages << ' ' << offset << '\n';
      assert(reader.loadPageAtOffset(offset, fence, lines, next, after));
      assert(next > offset && next <= text.content.size());
      for (const auto& line : lines) { std::cout << unsigned(line.headingLevel) << '\n'; recordText(line.text); }
      std::cout << "next " << next << ' ' << after << ' ' << work.measures << ' '
                << work.measuredBytes << ' ' << work.reads << ' ' << work.yields << '\n';
      assert(work.allocations == work.releases);
#ifdef ENFORCE_COST
      assert(work.boundaryBytes <= 8192);
#endif
      totalBoundaries += work.boundaries;
      totalBoundaryBytes += work.boundaryBytes;
      offset = next; fence = after;
      assert(++pages < 512);
    }
    std::cerr << "walk markdown=" << md << " pages=" << pages << " boundaries=" << totalBoundaries
              << " boundary_bytes=" << totalBoundaryBytes << '\n';
  }
  const std::vector<std::string> inputs = {
    "", "short", "\n\n", "one\r\ntwo\nthree\r\n", "   leading   and trailing    ",
    repeated("unbroken", 1000), repeated("é界 é ﬃ 😀 word ", 1700),
    "# Heading\n\nParagraph with **bold** and [link](path).\n## Next chapter\ntail\n",
    "```c\nlong code with spaces  and **literal** symbols\n```\nplain\n",
    "~~~\nalpha\n```\n# mixed-fence baseline\n",
    "> quote text\n- list item\n1. numbered item\n---\nafter\n",
    repeated("a ", 9000), std::string("ab\0cd ef\0gh", 11),
    std::string("a \x80\x81" "b \xc0\xaf z \xff y \xe2\x82"),
    std::string(80, char(0x80)), "word\tword\tword", repeated("\xf0\x9f\x98\x80", 8192),
    ">" + repeated("quoted words ", 8191), "- " + repeated("list words ", 8190)};
  // Explicit unfenced witnesses: the broad corpus below also enters inside
  // fences, which must not accidentally hide heading or quote coverage.
  reader.markdownMode = true;
  for (bool sd : {false, true}) {
    renderer.sd = sd;
    text.content = inputs[7];
    const auto heading = run(reader, "unfenced-heading");
    assert(heading.boldMeasures > 0);
    text.content = ">" + std::string(8191, 'Q');
    const auto quote = run(reader, "expanded-unfenced-quote");
    assert(quote.maxBoundaries == 8193);
#ifdef ENFORCE_COST
    assert(quote.boundaries == 8193 && quote.boundaryBytes == 8195);
#endif
  }
  for (bool md : {false, true}) for (bool sd : {false, true}) {
    reader.markdownMode = md; renderer.sd = sd;
    for (int width : {-1, 0, 1, 13, 400}) {
      reader.viewportWidth = width;
      reader.viewportHeight = width < 2 ? 0 : 640;
      reader.linesPerPage = width == 0 ? 0 : 18;
      for (size_t i = 0; i < inputs.size(); ++i) {
        text.content = inputs[i];
        run(reader, "edge-" + std::to_string(i), 0, i % 2);
        if (text.content.size() > 4) run(reader, "offset", 3, i % 2);
      }
    }
  }
  renderer.sd = false;
  reader.viewportWidth = 31; reader.viewportHeight = 180; reader.linesPerPage = 7;
  // Distinct deterministic malformed-byte mixtures include spaces followed by
  // continuation runs; every accepted byte is still passed to real metrics.
  uint32_t seed = 0x154873u;
  const unsigned char alphabet[] = {'a', ' ', 'b', '\t', 0x80, 0xbf, 0xc0, 0xc3, 0xa9, 0xe2, 0x82, 0xac, 0xff};
  for (unsigned i = 0; i < 192; ++i) {
    text.content.clear();
    for (unsigned j = 0; j < 64 + i; ++j) {
      seed = seed * 1664525u + 1013904223u;
      text.content += char(alphabet[(seed >> 16) % sizeof(alphabet)]);
    }
    reader.markdownMode = i % 2;
    run(reader, "mixed-" + std::to_string(i));
  }
  for (bool md : {false, true}) {
    reader.markdownMode = md;
    text.content = repeated(prose, 1000);
    failAllocation = true; run(reader, "allocation-failure");
    failAllocation = false; run(reader, "allocation-retry");
    text.failRead = true; run(reader, "read-failure");
    text.failRead = false; run(reader, "read-retry");
    run(reader, "eof", text.content.size());
    run(reader, "past-eof", text.content.size() + 3);
    reader.cachedFontId = 99; run(reader, "missing-font");
    reader.cachedFontId = 1; run(reader, "font-retry");
    text.content = "replacement at the same storage path"; run(reader, "changed-content");
  }
  std::cerr << "TXT paging cases=" << cases << " PASS\n";
}
