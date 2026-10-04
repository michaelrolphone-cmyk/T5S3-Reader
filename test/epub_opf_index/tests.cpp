#include "ContentOpfParser.h"
#include "fixtures.h"

#include <iostream>
#include <utility>

namespace {
size_t cases = 0;
bool outputsOnly = false;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void requireCost(bool condition, const std::string& message) {
  if (!outputsOnly) require(condition, message);
}

struct Item {
  std::string id;
  std::string href;
  std::string media = "application/xhtml+xml";
  std::string properties = "";
  bool includeId = true;
  bool includeHref = true;
};

std::string escape(const std::string& value) {
  std::string out;
  for (char ch : value) {
    if (ch == '&') out += "&amp;";
    else if (ch == '"') out += "&quot;";
    else if (ch == '<') out += "&lt;";
    else out += ch;
  }
  return out;
}

std::string manifest(const std::vector<Item>& items) {
  std::string xml = "<manifest>";
  for (const auto& item : items) {
    xml += "<item";
    if (item.includeId) xml += " id=\"" + escape(item.id) + "\"";
    if (item.includeHref) xml += " href=\"" + escape(item.href) + "\"";
    xml += " media-type=\"" + escape(item.media) + "\"";
    if (!item.properties.empty()) xml += " properties=\"" + escape(item.properties) + "\"";
    xml += "/>";
  }
  return xml + "</manifest>";
}

std::string spine(const std::vector<std::string>& references) {
  std::string xml = "<spine>";
  for (const auto& ref : references) xml += "<itemref idref=\"" + escape(ref) + "\"/>";
  return xml + "</spine>";
}

const std::string packageStart =
    "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"bookid\">"
    "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
    "<dc:identifier id=\"bookid\">urn:uuid:opf-index-regression</dc:identifier>"
    "<dc:title>Manifest fixture</dc:title><dc:title>Ignored subtitle</dc:title>"
    "<dc:creator>A</dc:creator><dc:language>en</dc:language>"
    "<meta property=\"dcterms:modified\">2026-10-04T00:00:00Z</meta>"
    "<meta name=\"cover\" content=\"cover\"/></metadata>";

std::string document(const std::vector<Item>& items, const std::vector<std::string>& references) {
  return packageStart + manifest(items) + spine(references) + "</package>";
}

std::vector<Item> numberedItems(size_t count) {
  std::vector<Item> items;
  items.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const auto number = std::to_string(1000 + i);
    items.push_back({"id" + number, "ch" + number + ".xhtml", "application/xhtml+xml", i == 0 ? "nav" : ""});
  }
  return items;
}

std::string normalizedHref(const Item& item) {
  return item.includeHref ? FsHelpers::normalisePath("OPS/" + item.href) : "";
}

size_t recordBytes(const Item& item) {
  return 8 + (item.includeId ? item.id.size() : 0) + normalizedHref(item).size();
}

size_t bytesThrough(const std::vector<Item>& items, size_t last) {
  size_t total = 0;
  for (size_t i = 0; i <= last; ++i) total += recordBytes(items[i]);
  return total;
}

struct Result {
  std::vector<std::string> spine;
  std::string title;
  std::string author;
  std::string language;
  std::string cover;
  std::string ncx;
  std::string nav;
  std::string guideCover;
  std::string textReference;
  std::vector<std::string> css;
  std::vector<uint8_t> stored;
  IoStats stats;
  bool parsed = false;
};

Result parse(const std::string& xml, size_t chunk = 1024, Faults injected = {}, bool sink = true,
             bool expectedSuccess = true) {
  Storage.reset();
  faults = injected;
  Result result;
  const std::string path = "/cache";
  const std::string base = "OPS/";
  BookMetadataCache cache;
  {
    ContentOpfParser parser(path, base, xml.size(), sink ? &cache : nullptr);
    require(parser.setup(), "Expat setup");
    result.parsed = true;
    for (size_t position = 0; position < xml.size();) {
      const size_t count = std::min(chunk, xml.size() - position);
      const size_t accepted = parser.write(reinterpret_cast<const uint8_t*>(xml.data()) + position, count);
      if (accepted != count) {
        require(accepted == 0, "parse failure returns zero");
        require(parser.write(static_cast<uint8_t>(' ')) == 0, "failed XML parser stays stopped");
        result.parsed = false;
        break;
      }
      position += count;
    }
    result.title = parser.title;
    result.author = parser.author;
    result.language = parser.language;
    result.cover = parser.coverItemHref;
    result.ncx = parser.tocNcxPath;
    result.nav = parser.tocNavPath;
    result.guideCover = parser.guideCoverPageHref;
    result.textReference = parser.textReferenceHref;
    result.css = parser.cssFiles;
    result.spine = cache.spine;
  }
  require(result.parsed == expectedSuccess, "expected XML success/failure in case " + std::to_string(cases + 1));
  require(Storage.files.empty(), "temporary manifest removed");
  require(io.liveHandles == 0 && io.opens == io.closes, "balanced open/close handles");
  require(io.peakHandles <= 1, "one temporary manifest handle at a time");
  result.stats = io;
  result.stored = Storage.lastRemovedBytes;
  if (expectedSuccess) {
    require(result.title == "Manifest fixture", "first title preserved");
    require(result.author == "A", "author preserved");
    require(result.language == "en", "language preserved");
  }
  ++cases;
  if (outputsOnly) {
    std::cout << "case " << cases << '\n';
    const auto field = [](const std::string& value) { std::cout << value.size() << ':' << value << '\n'; };
    for (const auto* value : {&result.title, &result.author, &result.language, &result.cover, &result.ncx,
                              &result.nav, &result.guideCover, &result.textReference}) field(*value);
    std::cout << "css " << result.css.size() << '\n';
    for (const auto& value : result.css) field(value);
    std::cout << "spine " << result.spine.size() << '\n';
    for (const auto& value : result.spine) field(value);
  }
  return result;
}

void expectSpine(const Result& result, const std::vector<std::string>& expected, const std::string& label) {
  require(result.spine == expected, label + ": exact ordered spine output");
}

void costRegression() {
  for (size_t chunk : {size_t(1), size_t(1024)}) {
    for (bool reverse : {false, true}) {
      for (size_t count : {size_t(16), size_t(32), size_t(64), size_t(127)}) {
        auto items = numberedItems(count);
        std::vector<std::string> refs;
        std::vector<std::string> expected;
        for (size_t j = 0; j < count; ++j) {
          const size_t i = reverse ? count - j - 1 : j;
          refs.push_back(items[i].id);
          expected.push_back(normalizedHref(items[i]));
        }
        const auto small = parse(document(items, refs), chunk);
        const auto large = parse(document(numberedItems(128), refs), chunk);
        expectSpine(small, expected, "small cost case");
        expectSpine(large, expected, "128-item control");
        require(small.nav == "OPS/ch1000.xhtml" && small.nav == large.nav, "nav metadata across threshold");
        require(small.cover.empty() && small.ncx.empty() && small.css.empty(), "absent metadata stays absent");
        const size_t readBytes = bytesThrough(items, count - 1);
        requireCost(small.stats.reads == 4 * count,
                "small manifest must use 4*N reads; original prefix scan uses 2*N*(N+1): N=" +
                    std::to_string(count) + ", actual=" + std::to_string(small.stats.reads));
        requireCost(small.stats.readBytes == readBytes, "small manifest each referenced record read once");
        requireCost(large.stats.reads == small.stats.reads && large.stats.readBytes == readBytes,
                "small/control logical read and byte counts");
        requireCost(small.stats.seeks == count && large.stats.seeks == count, "one successful seek per reference");
        requireCost(small.stats.writes == 4 * count && large.stats.writes == 4 * 128,
                "manifest encoding/write count unchanged");
        requireCost(small.stats.writeBytes == readBytes, "manifest serialized byte count");
        requireCost(small.stats.removed == 1 && large.stats.removed == 1, "temporary-file cleanup count");
      }
    }
  }
}

uint32_t fnv(const std::string& value) {
  uint32_t hash = 2166136261u;
  for (unsigned char ch : value) hash = (hash ^ ch) * 16777619u;
  return hash;
}

void duplicateCollisionAndMissingCases() {
  std::vector<Item> duplicates = {{"lead", "lead.xhtml"}, {"same", "first.xhtml"},
                                  {"other", "middle.xhtml"}, {"same", "later.xhtml"}};
  const auto duplicate = parse(document(duplicates, {"same", "same", "other"}));
  expectSpine(duplicate, {"OPS/first.xhtml", "OPS/first.xhtml", "OPS/middle.xhtml"}, "first duplicate");
  requireCost(duplicate.stats.reads == 12, "duplicates start at earliest possible candidate");

  const std::string first = "er86E4XNVp26";
  const std::string second = "gMoD9kMOMiYd";
  require(first != second && first.size() == second.size() && fnv(first) == 0x4c5f4ff9u && fnv(first) == fnv(second),
          "real equal-length FNV collision fixture");
  for (bool reversed : {false, true}) {
    const auto a = reversed ? second : first;
    const auto b = reversed ? first : second;
    std::vector<Item> items = {{"lead", "lead.xhtml"}, {a, "a.xhtml"}, {"gap", "gap.xhtml"},
                              {b, "b.xhtml"}, {b, "duplicate.xhtml"}};
    const auto collision = parse(document(items, {b, a, b}));
    expectSpine(collision, {"OPS/b.xhtml", "OPS/a.xhtml", "OPS/b.xhtml"}, "exact collision verification");
    requireCost(collision.stats.reads == 28, "collision retains original sequential decoder after first candidate");
    requireCost(collision.stats.seekOffsets == std::vector<size_t>(3, recordBytes(items[0])),
            "both colliding IDs start at earliest hash candidate");
  }

  const auto items = numberedItems(4);
  const auto missing = parse(document(items, {"absent", "id1003", "absent"}));
  expectSpine(missing, {"OPS/ch1003.xhtml"}, "missing IDs omitted");
  requireCost(missing.stats.reads == 36 && missing.stats.seekOffsets == std::vector<size_t>({0, 90, 0}),
          "missing key retains full original scan");
  const auto longMissing = parse(document(items, {std::string(4097, 'z')}));
  requireCost(longMissing.spine.empty() && longMissing.stats.reads == 16 && longMissing.stats.seekOffsets[0] == 0,
          "oversized idref retains full original scan");
}

void metadataAndEmptyCases() {
  std::vector<Item> items = {
      {"cover", "images/cover.jpg", "image/jpeg", "cover-image"},
      {"nav", "nav.xhtml", "application/xhtml+xml", "nav"},
      {"ncx", "toc.ncx", "application/x-dtbncx+xml"},
      {"ncx2", "ignored.ncx", "application/x-dtbncx+xml"},
      {"css1", "Styles/../main.css", "text/css"},
      {"css2", "other.css", "text/css"},
      {"chapter", "Text/../chapter.xhtml"},
  };
  const auto xml = packageStart + manifest(items) + spine({"chapter", "nav"}) +
      "<guide><reference type=\"start\" href=\"start.xhtml\"/>"
      "<reference type=\"text\" href=\"Text/../chapter.xhtml\"/>"
      "<reference type=\"cover\" href=\"cover.xhtml\"/></guide></package>";
  for (size_t chunk : {size_t(1), size_t(1024)}) {
    const auto result = parse(xml, chunk);
    expectSpine(result, {"OPS/chapter.xhtml", "OPS/nav.xhtml"}, "metadata fixture");
    require(result.cover == "OPS/images/cover.jpg" && result.nav == "OPS/nav.xhtml" &&
                result.ncx == "OPS/toc.ncx" && result.css == std::vector<std::string>({"OPS/main.css", "OPS/other.css"}) &&
                result.guideCover == "OPS/cover.xhtml" && result.textReference == "OPS/chapter.xhtml",
            "all exposed metadata and path normalization preserved");
  }

  std::vector<Item> empty = {{"", "first.xhtml"}, {"", "second.xhtml"}, {"nohref", "", "application/xhtml+xml", "", true, false},
                             {"emptyhref", ""}};
  const auto result = parse(document(empty, {"", "nohref", "emptyhref"}));
  expectSpine(result, {"OPS/first.xhtml", "", "OPS"}, "empty ID and omitted/empty href semantics");
  empty[0].includeId = false;
  const auto omitted = parse(document(empty, {""}));
  expectSpine(omitted, {"OPS/first.xhtml"}, "omitted manifest ID retains empty-ID selection");
  const auto emptyManifest = parse(document({}, {"missing"}));
  requireCost(emptyManifest.spine.empty() && emptyManifest.stats.reads == 0, "empty manifest");
  const auto noRefs = parse(document(numberedItems(3), {}));
  requireCost(noRefs.stats.reads == 0 && noRefs.stats.seeks == 0, "empty spine");
  const auto nullSink = parse(document(numberedItems(3), {"id1002"}), 1, {}, false);
  requireCost(nullSink.spine.empty() && nullSink.stats.reads == 0 && nullSink.stats.seeks == 0 &&
              nullSink.nav == "OPS/ch1000.xhtml", "null sink skips resolution and preserves metadata");
  const auto missingIdref = parse(packageStart + manifest(numberedItems(2)) + "<spine><itemref/></spine></package>");
  requireCost(missingIdref.stats.reads == 0 && missingIdref.stats.seeks == 0, "itemref without idref");
}

void boundedEligibilityCases() {
  auto items = numberedItems(3);
  for (size_t length : {size_t(4096), size_t(4097), size_t(65536)}) {
    items[2].id.assign(length, 'x');
    const auto result = parse(document(items, {items[2].id}));
    expectSpine(result, {"OPS/ch1002.xhtml"}, "long ID");
    requireCost(result.stats.reads == (length <= 4096 ? 4u : 12u), "ID eligibility boundary");
  }
  items = numberedItems(3);
  for (size_t length : {size_t(4096), size_t(4097)}) {
    // Include the normalized OPS/ prefix in the field bound.
    items[0].href.assign(length - 4, 'h');
    const auto result = parse(document(items, {items[2].id}));
    expectSpine(result, {"OPS/ch1002.xhtml"}, "long unrelated href");
    requireCost(result.stats.reads == (length <= 4096 ? 4u : 12u), "href field eligibility boundary");
  }

  // 32 individually legal records can total exactly the 128 KiB optional cap.
  for (size_t extra : {size_t(0), size_t(1)}) {
    items = numberedItems(32);
    for (size_t i = 0; i < items.size(); ++i) {
      const size_t targetRecordBytes = 4096 + (i == 0 ? extra : 0);
      items[i].href.assign(targetRecordBytes - 8 - items[i].id.size() - 4, 'p');
    }
    require(bytesThrough(items, 31) == 128 * 1024 + extra, "exact total-file boundary fixture");
    const auto result = parse(document(items, {items.back().id}));
    expectSpine(result, {normalizedHref(items.back())}, "file size bound");
    requireCost(result.stats.reads == (extra == 0 ? 4u : 128u), "total-file eligibility boundary");
  }

  // Eligibility rejection is confined to the optional small path; existing
  // >=128 binary-index behavior still resolves a long-ID candidate directly.
  items = numberedItems(128);
  items.back().id.assign(4097, 'q');
  const auto large = parse(document(items, {items.back().id}));
  expectSpine(large, {normalizedHref(items.back())}, "large index unchanged");
  requireCost(large.stats.reads == 4 && large.stats.seeks == 1, ">=128 path unaffected by small limits");
  const auto bigger = parse(document(numberedItems(257), {"id1256", "id1000", "id1128"}));
  expectSpine(bigger, {"OPS/ch1256.xhtml", "OPS/ch1000.xhtml", "OPS/ch1128.xhtml"}, "large sorted index");
  requireCost(bigger.stats.reads == 12, "large binary-index reads");

  // Stale old-generation offsets would select the second duplicate incorrectly.
  const std::vector<Item> oldItems = {{"ol", "x"}, {"same", "old"}};
  const std::vector<Item> newItems = {{"same", ""}, {"same", "later.xhtml"}};
  require(recordBytes(oldItems[0]) == recordBytes(newItems[0]), "stale offset targets later duplicate");
  const auto repeated = parse(packageStart + manifest(oldItems) + manifest(newItems) + spine({"same"}) + "</package>");
  expectSpine(repeated, {"OPS"}, "repeated manifest generation");
  requireCost(repeated.stats.seekOffsets == std::vector<size_t>({0}), "repeated manifest disables stale cursor hint");
}

void appendSerialized(std::vector<uint8_t>& bytes, const std::string& value) {
  const uint32_t length = static_cast<uint32_t>(value.size());
  const auto* start = reinterpret_cast<const uint8_t*>(&length);
  bytes.insert(bytes.end(), start, start + sizeof(length));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

void faultAndCleanupCases() {
  const auto items = numberedItems(3);
  const auto xml = document(items, {"id1001"});
  for (int scenario = 0; scenario < 4; ++scenario) {
    Faults fault;
    if (scenario == 0) fault.failedCloseCall = 1;
    if (scenario == 1) fault.reportedExtraSize = 1;
    if (scenario == 2) fault.errorAfterWriteCall = 4;
    if (scenario == 3) fault.errorOnReadOpen = true;
    const auto result = parse(xml, 1024, fault);
    expectSpine(result, {"OPS/ch1001.xhtml"}, "eligibility failure still uses legacy scan");
    requireCost(result.stats.reads == 8 && result.stats.seekOffsets == std::vector<size_t>({0}),
            "write/close/reopen eligibility failure falls back");
    requireCost(result.stats.writes == 12, "eligibility failure does not suppress writes");
  }

  // Truncate only the *last* unreferenced href body. The requested earlier
  // record is intact. Reading the corrupt tail would invoke inherited unchecked
  // serialization and is deliberately not treated as a defined error oracle.
  Faults partial;
  partial.partialWriteCall = 12;
  partial.partialWriteBytes = normalizedHref(items.back()).size() - 1;
  const auto partialResult = parse(xml, 1024, partial);
  expectSpine(partialResult, {"OPS/ch1001.xhtml"}, "partial later write disables hint");
  requireCost(partialResult.stats.reads == 8 && partialResult.stats.seekOffsets[0] == 0 && partialResult.stats.writes == 12,
          "partial write uses unchanged earlier-record decoder and all original writes");
  std::vector<uint8_t> expected;
  for (const auto& item : items) {
    appendSerialized(expected, item.id);
    appendSerialized(expected, normalizedHref(item));
  }
  expected.pop_back();
  requireCost(partialResult.stored == expected && partialResult.stats.writeBytes == expected.size(),
          "partial-write bytes and encoding retained exactly");

  Faults seek;
  seek.failedNonzeroSeek = 2;
  const auto fallback = parse(document(items, {"id1002", "id1001"}), 1024, seek);
  expectSpine(fallback, {"OPS/ch1002.xhtml", "OPS/ch1001.xhtml"}, "failed nonzero seek fallback");
  requireCost(fallback.stats.reads == 12 && fallback.stats.seekOffsets == std::vector<size_t>({60, 30, 0}),
          "one bounded fallback seek from prior EOF");

  Faults reopen;
  reopen.failReadOpen = true;
  const auto failedOpen = parse(xml, 1024, reopen);
  requireCost(failedOpen.spine.empty() && failedOpen.stats.reads == 0 && failedOpen.stats.seekOffsets == std::vector<size_t>({0}),
          "failed read reopen cannot use stale nonzero hint");

  Faults unavailable;
  unavailable.unavailableReads = true;
  unavailable.failAllSeeks = true;
  const auto stopped = parse(xml, 1024, unavailable, true, false);
  requireCost(stopped.spine.empty() && stopped.stats.reads == 0 && stopped.stats.seeks == 2,
          "unavailable input has at most one optional retry and no decode loop");

  for (bool seekMoves : {false, true}) {
    Faults poisoned;
    poisoned.failedNonzeroSeek = 1;
    poisoned.poisonFailedSeek = true;
    poisoned.poisonedSeekCanMove = seekMoves;
    const auto aborted = parse(xml, 1024, poisoned, true, false);
    require(aborted.spine.empty(), "poisoned optional seek never publishes stale href");
    requireCost(aborted.stats.reads == 0 && aborted.stats.seekOffsets == std::vector<size_t>({30, 0}) &&
                    aborted.stats.removed == 1,
                "poisoned optional seek stops before decoder after one bounded retry");
    const auto retried = parse(xml);
    expectSpine(retried, {"OPS/ch1001.xhtml"}, "fresh parser recovers after poisoned seek");
    requireCost(retried.stats.reads == 4, "fresh poisoned-seek retry restores optimized path");
  }

  Faults noWriter;
  noWriter.failWriteOpen = true;
  const auto failedWriteOpen = parse(document({}, {}), 1024, noWriter);
  requireCost(failedWriteOpen.stats.opens == 0 && failedWriteOpen.stats.reads == 0, "empty-manifest write-open failure cleanup");

  for (size_t chunk : {size_t(1), size_t(1024)}) {
    const auto malformed = parse(packageStart + manifest(items) + "<spine><itemref idref=\"id1001\"/></broken>",
                                 chunk, {}, true, false);
    expectSpine(malformed, {"OPS/ch1001.xhtml"}, "malformed XML retains already delivered entries");
    requireCost(malformed.stats.removed == 1, "XML failure removes temporary manifest");
    const auto retried = parse(xml, chunk);
    expectSpine(retried, {"OPS/ch1001.xhtml"}, "fresh parser after failure");
    requireCost(retried.stats.reads == 4, "fresh parser resets hint state");
  }
  for (int repetition = 0; repetition < 3; ++repetition) {
    const auto repeated = parse(document(items, {"id1002", "id1000", "id1002"}), 1);
    expectSpine(repeated, {"OPS/ch1002.xhtml", "OPS/ch1000.xhtml", "OPS/ch1002.xhtml"}, "repeat/new-parser cleanup");
    requireCost(repeated.stats.reads == 12 && repeated.stats.removed == 1, "repeat counts and cleanup");
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--outputs-only") outputsOnly = true;
  else require(argc == 1, "supported test arguments");
  costRegression();
  duplicateCollisionAndMissingCases();
  metadataAndEmptyCases();
  boundedEligibilityCases();
  if (!outputsOnly) faultAndCleanupCases();
  if (outputsOnly) {
    std::cout << "PASS: " << cases << " healthy output snapshots; metadata, spine, and cleanup\n";
  } else {
    std::cout << "PASS: " << cases << " complete-parser OPF index cases; exact output, I/O cost, and cleanup\n";
  }
}
