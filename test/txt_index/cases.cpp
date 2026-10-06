static const risc_storage_volume_api_v1_ext* underlying;
static unsigned opens, closes, seeks, reads, liveFiles;
static size_t payload;
static bool rejectOpen, rejectSeek, zeroRead, shortRead, rejectClose;
static risc_storage_file_t countedOpen(void* ctx, const char* path, uint32_t flags) {
  ++opens;
  if (rejectOpen) return 0;
  auto h = underlying->file_open(ctx, path, flags);
  if (h) ++liveFiles;
  return h;
}
static bool countedClose(void* ctx, risc_storage_file_t h, bool commit) {
  ++closes;
  if (rejectClose) return false;
  const bool ok = underlying->base.file_close(ctx, h, commit);
  if (ok) { assert(liveFiles); --liveFiles; }
  return ok;
}
static bool countedSeek(void* ctx, risc_storage_file_t h, uint64_t offset) {
  ++seeks;
  return !rejectSeek && underlying->file_seek(ctx, h, offset);
}
static size_t countedRead(void* ctx, risc_storage_file_t h, void* buf, size_t n) {
  ++reads;
  if (zeroRead) return 0;
  if (shortRead) { shortRead = false; zeroRead = true; n = std::min<size_t>(n, 7); }
  const size_t got = underlying->base.file_read(ctx, h, buf, n);
  payload += got;
  return got;
}
static void resetCounts() { opens = closes = seeks = reads = 0; payload = 0; taskYields = 0; }
static Txt createBook(const std::string& bytes) {
  assert(liveFiles == 0);
  if (Storage.exists("/book.txt")) assert(Storage.remove("/book.txt"));
  auto w = Storage.open("/book.txt", O_WRONLY | O_CREAT); assert(w);
  const size_t written = w.write(bytes.data(), bytes.size());
  if (written != bytes.size()) std::fprintf(stderr, "fixture write got=%zu want=%zu clock=%llu\n", written, bytes.size(), (unsigned long long)card_time);
  assert(written == bytes.size()); assert(w.close());
  Txt txt("/book.txt", "/cache"); assert(txt.load()); return txt;
}
struct Page { size_t next; bool fence; std::vector<std::pair<std::string, uint8_t>> lines; };
static std::vector<Page> paginate(TxtReaderActivity& reader, Txt::ReadWindow* window) {
  std::vector<Page> result;
  bool fence = false;
  for (size_t at = 0; at < reader.txt->getFileSize();) {
    std::vector<TxtDisplayLine> lines; size_t next = at; bool after = fence;
    assert(reader.loadPageAtOffset(at, fence, lines, next, after, window));
    assert(next > at && next <= reader.txt->getFileSize());
    Page page{next, after, {}};
    for (const auto& line : lines) page.lines.emplace_back(line.text, line.headingLevel);
    result.push_back(std::move(page)); at = next; fence = after;
  }
  return result;
}
static void compare(Txt& txt, bool markdown, bool sd) {
  TxtReaderActivity original; original.txt = &txt; original.markdownMode = markdown; original.renderer.sd = sd;
  auto expected = paginate(original, nullptr);
  TxtReaderActivity reader; reader.txt = &txt; reader.markdownMode = markdown; reader.renderer.sd = sd;
  Txt::ReadWindow window(txt);
  auto actual = paginate(reader, &window);
  assert(window.close());
  assert(expected.size() == actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    assert(expected[i].next == actual[i].next && expected[i].fence == actual[i].fence);
    assert(expected[i].lines == actual[i].lines);
  }
  assert(original.renderer.prepared == reader.renderer.prepared);
  assert(liveFiles == 0);
}
int main() {
  card_image = static_cast<uint8_t*>(calloc(card_sectors, 512)); assert(card_image); format(false);
  const auto* driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
  underlying = risc_storage_volume_extension(static_cast<const risc_storage_volume_api_v1*>(driver->capability));
  risc_platform_clock_api_v1 clock{1, sizeof(clock), nullptr, now, sleep};
  risc_provider_dependency_v1 deps[] = {{"platform.clock", 1, &clock}};
  assert(driver->start(deps, 1));
  auto counted = *underlying; counted.base.struct_size = sizeof(counted);
  counted.file_open = countedOpen; counted.base.file_close = countedClose;
  counted.file_seek = countedSeek; counted.base.file_read = countedRead;
  assert(Storage.bindVolume(&counted.base));

  for (size_t kib : {64u, 128u, 256u, 512u}) {
    std::string bytes(kib * 1024, 'x');
    for (size_t i = 31; i < bytes.size(); i += 32) bytes[i] = '\n';
    auto txt = createBook(bytes);
    TxtReaderActivity reader; reader.txt = &txt;
    resetCounts(); const auto baselineSectors = card_reads;
    const auto reference = paginate(reader, nullptr);
    assert(reference.size() == kib);
    assert(payload == 8192 * kib - 28672 && opens == kib && closes == kib);
    printf("original %zu KiB pages=%zu payload=%zu sectors=%u opens=%u seeks=%u\n",
           kib, reference.size(), payload, card_reads - baselineSectors, opens, seeks);
    resetCounts(); const auto sectors = card_reads;
    assert(reader.buildPageIndex());
    assert(reader.pageOffsets.size() == kib && reader.pageFenceOpen.size() == kib);
    for (size_t i = 0; i < kib; ++i) { assert(reader.pageOffsets[i] == i * 1024); assert(!reader.pageFenceOpen[i]); }
    assert(payload == bytes.size() && "original overlapping reads exceed one-pass payload bound");
    assert(opens == 1 && closes == 1 && seeks == 1 && liveFiles == 0);
    assert(card_reads - sectors < 3 * kib && taskYields >= kib / 8);
    printf("window %zu KiB pages=%zu payload=%zu sectors=%u opens=%u seeks=%u\n",
           kib, reader.pageOffsets.size(), payload, card_reads - sectors, opens, seeks);
    compare(txt, false, true);
  }
  for (const std::string& bytes : std::vector<std::string>{
      "a", "last line without newline", "\r\n\nalpha\r\nbeta\n", std::string(16387, 'z'),
      std::string(8190, 'x') + "\xe4\xb8\xad\xe6\x96\x87\nend\n",
      "# Title\n\nIntro words words\n```\n# code, not heading\n```\n## Next\n---\nend\n",
      std::string(9000, '\n'), std::string(4090, ' ') + "\n\xe4\xb8\xad\xe6\x96\x87"}) {
    auto txt = createBook(bytes);
    compare(txt, false, false); compare(txt, true, true);
  }
  for (const std::string& bytes : {std::string(), std::string("tiny")}) {
    auto small = createBook(bytes); TxtReaderActivity reader; reader.txt = &small;
    resetCounts(); assert(reader.buildPageIndex());
    assert(lastWindowAllocation == bytes.size() + 1 && reader.pageOffsets.size() == 1);
    assert(liveFiles == 0 && opens == (bytes.empty() ? 0u : 1u));
  }
  { // Slow metrics trigger elapsed checkpoints, including unsigned clock rollover.
    std::string bytes(4096, 'x'); for (size_t i=31; i<bytes.size(); i+=32) bytes[i]='\n';
    auto book = createBook(bytes); TxtReaderActivity reader; reader.txt = &book;
    card_time = UINT32_MAX - 10; metricDelay = 21; taskYields = 0;
    assert(reader.buildPageIndex() && taskYields >= 4); metricDelay = 0;
  }
  auto txt = createBook(std::string(20000, 'a'));
  { // Exact shorter, backward, disjoint, repeated and EOF requests.
    Txt::ReadWindow w(txt);
    for (auto request : std::vector<std::pair<size_t, size_t>>{{0,8192},{1,5},{6,8192},{0,1},{17000,3000},{19000,1000},{19000,1000}}) {
      const auto* data = w.read(request.first, request.second); assert(data);
      assert(std::string(reinterpret_cast<const char*>(data), request.second) == std::string(request.second, 'a'));
    }
    assert(!w.read(20001, 1) && !w.read(19999, 2) && !w.read(0, 8193) && !w.read(0, 0));
    assert(w.close() && !w.read(0, 1));
  }
  { Txt unloaded("/book.txt", "/cache"); Txt::ReadWindow w(unloaded); assert(!w.read(0,1)); }
  for (unsigned fault = 0; fault < 4; ++fault) {
    rejectOpen = fault == 0; rejectSeek = fault == 1; zeroRead = fault == 2; shortRead = fault == 3;
    { Txt::ReadWindow w(txt); assert(!w.read(0,8192)); assert(!w.read(0,8192)); }
    rejectOpen = rejectSeek = zeroRead = shortRead = false;
    assert(liveFiles == 0);
    { Txt::ReadWindow retry(txt); assert(retry.read(0,8192)); assert(retry.close()); }
  }
  { // A generation change invalidates even a wholly cached EOF suffix.
    Txt::ReadWindow w(txt); assert(w.read(11808,8192));
    Storage.invalidateObservations();
    assert(!w.read(19000,1000) && liveFiles == 0);
  }
  { Txt::ReadWindow retry(txt); assert(retry.read(19000,1000)); }
  { // No partial successful index on read failure; checked return prevents persistence.
    TxtReaderActivity reader; reader.txt = &txt; zeroRead = true;
    assert(!reader.buildPageIndex()); zeroRead = false;
    assert(reader.buildPageIndex() && liveFiles == 0);
  }
  {
    failWindowAllocation = true;
    { Txt::ReadWindow w(txt); assert(!w.read(0,1)); }
    failWindowAllocation = false;
    { Txt::ReadWindow retry(txt); assert(retry.read(0,1)); }
  }
  { // Font preparation uses read-only handles: these do not invalidate a window.
    Txt::ReadWindow w(txt); assert(w.read(0,8192));
    const auto stamp = Storage.generation();
    auto metrics = Storage.open("/book.txt"); assert(metrics);
    char glyphBytes[16]; assert(metrics.read(glyphBytes, sizeof(glyphBytes)) == sizeof(glyphBytes));
    assert(Storage.unchanged(stamp) && w.read(1024,8192));
    assert(metrics.close() && w.close() && liveFiles == 0);
  }
  { // Generation changes after the last page also prevent a successful close.
    Txt::ReadWindow w(txt); assert(w.read(19000,1000));
    Storage.invalidateObservations(); assert(!w.close());
  }
  { // An active unrelated writer makes cached observations ineligible.
    Txt::ReadWindow w(txt); assert(w.read(19000,1000));
    auto writer = Storage.open("/other.txt", O_WRONLY | O_CREAT); assert(writer);
    assert(!w.read(19000,1000)); assert(writer.close());
  }
  { Txt::ReadWindow retry(txt); assert(retry.read(19000,1000)); }
  { // Seek failure after a valid window also closes and forbids cache reuse.
    Txt::ReadWindow w(txt); assert(w.read(1000,8192)); rejectSeek = true;
    assert(!w.read(0,1)); rejectSeek = false; assert(!w.read(1000,8192));
  }
  { // Checked close failure remains failed and is retried on destruction.
    Txt::ReadWindow w(txt); assert(w.read(0,8192)); rejectClose = true;
    assert(!w.close() && liveFiles == 1); rejectClose = false;
  }
  assert(liveFiles == 0);
  // Failed close deliberately poisons generation reuse, as the production HAL requires.
  { Txt::ReadWindow w(txt); assert(!w.read(0,1)); }
  assert(windowAllocations == windowFrees);
  puts("TXT page/window equivalence, boundaries, generation, read/open/seek/close faults and retry PASS");
  free(card_image);
}
