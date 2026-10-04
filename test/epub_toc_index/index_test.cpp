#include "BookMetadataCache.h"
#include "TocNavParser.h"
#include <Serialization.h>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>

static const std::string spinePath = "/cache/spine.bin.tmp";
static const std::string tocPath = "/cache/toc.bin.tmp";

static std::string href(unsigned n) {
  char name[80];
  std::snprintf(name, sizeof(name), "OPS/chapter-%03u.xhtml", n);
  return name;
}

static void reset() {
  for (const auto& pair : io) assert(pair.second.live == 0);
  Storage.files.clear(); io.clear(); yields = 0; ticks = 0; readTickCost = 0;
  failReadAt = shortReadAt = failSeekAt = -1;
  Storage.failReadOpens = Storage.failWriteOpens = 0;
}

static void writeSpine(BookMetadataCache& cache, const std::vector<std::string>& paths) {
  assert(cache.beginWrite());
  assert(cache.beginContentOpfPass());
  for (const auto& path : paths) cache.createSpineEntry(path);
  assert(cache.endContentOpfPass());
}

static std::vector<uint8_t> run(unsigned chapters, unsigned sections, size_t chunk, unsigned linked = 0) {
  if (!linked) linked = chapters;
  assert(linked <= chapters);
  reset();
  BookMetadataCache cache("/cache");
  std::vector<std::string> paths;
  for (unsigned i = 0; i < chapters; ++i) paths.push_back(href(i));
  writeSpine(cache, paths);
  std::string xml = "<html xmlns:epub=\"http://www.idpf.org/2007/ops\"><body><nav epub:type=\"toc\"><ol>";
  // Reverse and interleave chapter links: the bound must not rely on adjacent
  // equal hrefs, anchors, navigation order or parser chunk size.
  for (unsigned s = 0; s < sections; ++s) for (unsigned n = linked; n > 0; --n) {
    unsigned c = n - 1;
    xml += "<li><a href=\"" + href(c).substr(4) + "#section-" + std::to_string(s) +
           "\">Chapter " + std::to_string(c) + " section " + std::to_string(s) + "</a></li>";
  }
  xml += "</ol></nav></body></html>";
  assert(cache.beginTocPass());
  const Io indexing = io[spinePath];
  {
    const std::string base = "OPS/";
    TocNavParser parser(base, xml.size(), &cache);
    assert(parser.setup());
    for (size_t p = 0; p < xml.size();) {
      const size_t n = std::min(chunk, xml.size() - p);
      assert(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + p), n) == n);
      p += n;
    }
  }
  assert(cache.getTocCount() == int(linked * sections));
  assert(cache.endTocPass());
  const Io observed = io[spinePath];
  FsFile file;
  assert(Storage.openFileForRead("test", tocPath, file));
  for (unsigned s = 0; s < sections; ++s) for (unsigned n = linked; n > 0; --n) {
    unsigned c = n - 1;
    std::string title, path, anchor; uint8_t level; int16_t spine;
    serialization::readString(file, title); serialization::readString(file, path);
    serialization::readString(file, anchor); serialization::readPod(file, level);
    serialization::readPod(file, spine);
    assert(title == "Chapter " + std::to_string(c) + " section " + std::to_string(s));
    assert(path == href(c)); assert(anchor == "section-" + std::to_string(s));
    assert(spine == int(c)); assert(level == 1);
  }
  assert(!file.available()); assert(file.close()); assert(cache.endWrite());
#ifdef PERF_EXPECT_BASELINE
  const uint64_t rows = chapters < 128 ? uint64_t(sections) * linked * (linked + 1) / 2 : chapters;
#else
  const uint64_t rows = chapters;
#endif
  if (observed.reads != rows * 4) {
    std::cerr << "spine read bound failed: " << observed.reads << " vs " << rows * 4 << '\n';
    std::abort();
  }
  std::cout << "chapters=" << chapters << " toc_entries=" << linked * sections << " chunk=" << chunk
            << " spine_reads=" << observed.reads << " per_toc_reads=" << observed.reads - indexing.reads
            << " seeks=" << observed.seeks << " yields=" << yields << " output_mapping=PASS\n";
  const auto result = *Storage.files.at(tocPath);
  assert(cache.cleanupTmpFiles()); assert(Storage.files.empty());
  return result;
}

#ifndef PERF_EXPECT_BASELINE
static void checkEntries(BookMetadataCache& cache, const std::vector<std::string>& links,
                         const std::vector<int>& expected) {
  for (size_t i = 0; i < links.size(); ++i) cache.createTocEntry("Title", links[i], "fragment", uint8_t(i));
  FsFile file;
  assert(Storage.openFileForRead("test", tocPath, file));
  for (size_t i = 0; i < links.size(); ++i) {
    std::string title, path, anchor; uint8_t level; int16_t index;
    serialization::readString(file, title); serialization::readString(file, path);
    serialization::readString(file, anchor); serialization::readPod(file, level);
    serialization::readPod(file, index);
    assert(title == "Title" && path == links[i] && anchor == "fragment" && level == i);
    assert(index == expected[i]);
  }
  assert(!file.available());
}

static void edgeCases() {
  reset();
  {
    BookMetadataCache cache("/cache");
    writeSpine(cache, {"same.xhtml", "same.xhtml", "Same.xhtml", "", "last.xhtml"});
    assert(cache.beginTocPass()); const auto reads = io[spinePath].reads;
    checkEntries(cache, {"same.xhtml", "Same.xhtml", "", "missing.xhtml", "last.xhtml", "same.xhtml"},
                 {0, 2, 3, -1, 4, 0});
    assert(io[spinePath].reads == reads);
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
    // Reuse the same cache instance; the old complete index must not survive.
    writeSpine(cache, {"different.xhtml", "same.xhtml"});
    Storage.failReadOpens = 1; assert(!cache.beginTocPass());
    assert(io[spinePath].live == 0 && io[tocPath].live == 0);
    Storage.failWriteOpens = 1; assert(!cache.beginTocPass());
    assert(io[spinePath].live == 0 && io[tocPath].live == 0);
    assert(cache.beginTocPass());
    checkEntries(cache, {"same.xhtml", "different.xhtml", "last.xhtml"}, {1, 0, -1});
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
    // Empty spine retains the missing target result.
    writeSpine(cache, {}); assert(cache.beginTocPass());
    checkEntries(cache, {"missing.xhtml"}, {-1});
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
  }
  // The exact payload budget is inclusive, including embedded NUL/UTF-8 bytes.
  reset();
  {
    BookMetadataCache cache("/cache");
    const std::string path = std::string(8182, 'x') + std::string("\0caf\xc3\xa9.bin", 10);
    assert(path.size() == 8192);
    writeSpine(cache, {path}); assert(cache.beginTocPass());
    const auto reads = io[spinePath].reads;
    checkEntries(cache, {path, "missing"}, {0, -1});
    assert(io[spinePath].reads == reads);
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
  }
  // Oversize href/aggregate budget uses the old scan without caching a prefix.
  for (const auto& paths : std::vector<std::vector<std::string>>{
           {std::string(8193, 'x'), "last"}, {std::string(4096, 'x'), std::string(4096, 'y'), "last"}}) {
    reset(); BookMetadataCache cache("/cache"); writeSpine(cache, paths);
    assert(cache.beginTocPass()); const auto reads = io[spinePath].reads;
    checkEntries(cache, {paths[0], paths.back(), "missing"}, {0, int(paths.size() - 1), -1});
    assert(io[spinePath].reads > reads);
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
  }
  // Any one-off failed/short component read discards the partial optimization.
  for (int fault = 1; fault <= 8; ++fault) for (bool shortRead : {false, true}) {
    reset(); BookMetadataCache cache("/cache"); writeSpine(cache, {"first", "last"});
    if (shortRead) shortReadAt = fault; else failReadAt = fault;
    assert(cache.beginTocPass()); const auto reads = io[spinePath].reads;
    checkEntries(cache, {"last", "first", "missing"}, {1, 0, -1});
    assert(io[spinePath].reads > reads);
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
  }
  // A failed optional-cache rewind is harmless for a complete memory lookup;
  // a partial/error cursor is rewound again by the unchanged fallback scan.
  for (bool partial : {false, true}) {
    reset(); BookMetadataCache cache("/cache"); writeSpine(cache, {"first", "last"});
    failSeekAt = 1;
    if (partial) failReadAt = 5;
    assert(cache.beginTocPass());
    const auto reads = io[spinePath].reads;
    checkEntries(cache, {"last", "first", "missing"}, {1, 0, -1});
    assert(partial ? io[spinePath].reads > reads : io[spinePath].reads == reads);
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
  }
  // Re-enter TOC admission after a large-index pass, including open failures.
  // A still-set old index must not admit an entry through a closed spine.
  reset();
  {
    BookMetadataCache cache("/cache"); std::vector<std::string> paths;
    for (unsigned i = 0; i < 128; ++i) paths.push_back(href(i));
    writeSpine(cache, paths); assert(cache.beginTocPass());
    Storage.failReadOpens = 1; assert(!cache.beginTocPass());
    cache.createTocEntry("ignored", href(127), "", 0); assert(cache.getTocCount() == 0);
    Storage.failWriteOpens = 1; assert(!cache.beginTocPass());
    assert(io[spinePath].live == 0 && io[tocPath].live == 0);
    cache.createTocEntry("ignored", href(127), "", 0); assert(cache.getTocCount() == 0);
    assert(cache.beginTocPass()); checkEntries(cache, {href(127), href(0), "missing"}, {127, 0, -1});
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
  }
  // Budget fallback and elapsed-time scheduler cooperation across tick wrap.
  reset();
  {
    BookMetadataCache cache("/cache"); std::vector<std::string> paths;
    for (unsigned i = 0; i < 32; ++i) paths.push_back(href(i));
    writeSpine(cache, paths); ticks = UINT32_MAX - 100; readTickCost = 10;
    assert(cache.beginTocPass()); assert(yields > 0); const auto reads = io[spinePath].reads;
    assert(reads < 32 * 4); readTickCost = 0;
    checkEntries(cache, {href(31), href(0), "missing"}, {31, 0, -1});
    assert(io[spinePath].reads > reads);
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
    writeSpine(cache, {"one", "two"}); readTickCost = 6; yields = 0;
    assert(cache.beginTocPass()); assert(yields == 2);
    checkEntries(cache, {"two", "one"}, {1, 0});
    assert(cache.endTocPass()); assert(cache.endWrite()); assert(cache.cleanupTmpFiles());
  }
  reset();
  std::cout << "exact_duplicates_missing_empty_budget_read_faults_open_failures_retry_cleanup_tick_wrap=PASS\n";
}
#endif

int main() {
  for (unsigned c : {1u, 32u, 64u, 100u, 127u, 128u, 200u}) run(c, 10, 4096);
  run(127, 10, 1); run(128, 10, 1);
  const auto exact127 = run(127, 10, 4096, 127);
  const auto extra128 = run(128, 10, 4096, 127);
  assert(exact127 == extra128);
  if (const char* output = std::getenv("TOC_OUTPUT_PATH")) {
    std::ofstream file(output, std::ios::binary);
    file.write(reinterpret_cast<const char*>(exact127.data()), exact127.size());
    assert(file.good());
  }
  std::cout << "identical_1270_link_nav_extra_unlisted_spine=byte_identical_toc_PASS\n";
#ifndef PERF_EXPECT_BASELINE
  edgeCases();
#endif
}
