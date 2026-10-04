#include <HalStorage.h>
#include <MD5Builder.h>
#include <KOReaderDocumentId.h>
#include <iostream>

enum class DocumentMatchMethod { FILENAME, BINARY };
struct StoreFixture {
  DocumentMatchMethod method = DocumentMatchMethod::BINARY;
  DocumentMatchMethod getMatchMethod() const { return method; }
} KOREADER_STORE;
enum { SYNCING, SYNC_FAILED, STR_HASH_FAILED };
struct Sync {
  std::string documentHash = "previous-identity", epubPath = "/Books/test.epub";
  int state = SYNCING, statusMessage = -1, updates = 0, networkCalls = 0;
  struct RenderLock { explicit RenderLock(Sync&) {} };
  static int tr(int message) { return message; }
  void requestUpdate(bool) { ++updates; }
  void run() {
#include <sync_dispatch.inc>
    ++networkCalls;
  }
};

static constexpr size_t sampleOffsets[] = {0, 1024, 4096, 16384, 65536, 262144, 1048576,
                                          4194304, 16777216, 67108864, 268435456, 1073741824};
static unsigned cases = 0;
static void resetCase(size_t size, Fixture::Fault fault = Fixture::Fault::None, int sample = 0) {
  Fixture::reset(size, fault, sample); HashFixture::reset();
}
static void healthy(size_t size) {
  resetCase(size);
  assert(KOReaderDocumentId::calculate("/Books/test.epub") == HashFixture::digest);
  assert(Fixture::opens == 1 && Fixture::live == 0 && Fixture::closes == 1);
  assert(HashFixture::begins == 1 && HashFixture::finalizes == 1 && HashFixture::publishes == 1);
  std::vector<uint8_t> expected;
  std::vector<size_t> offsets, requests;
  for (size_t offset : sampleOffsets) {
    if (offset >= size) continue;
    offsets.push_back(offset);
    const size_t count = std::min(size_t{1024}, size - offset);
    requests.push_back(count);
    for (size_t i = 0; i < count; ++i) expected.push_back(Fixture::byteAt(offset + i));
  }
  assert(Fixture::offsets == offsets && Fixture::requests == requests);
  assert(HashFixture::input == expected);
  assert(Fixture::seeks <= 12 && Fixture::reads <= 12 && expected.size() <= 12 * 1024);
  ++cases;
}
static void failed(size_t size, Fixture::Fault fault, int sample) {
  resetCase(size, fault, sample);
  const auto result = KOReaderDocumentId::calculate("/Books/test.epub");
  if (!result.empty()) {
    std::cerr << "required sample failure published a document ID; fault=" << static_cast<int>(fault)
              << " sample=" << sample << " size=" << size << '\n';
    std::exit(1);
  }
  assert(Fixture::opens == 1 && Fixture::live == 0);
  assert(Fixture::closes == (fault == Fixture::Fault::Open ? 0 : 1));
  assert(HashFixture::finalizes == 0 && HashFixture::publishes == 0);
  if (fault != Fixture::Fault::Open) {
    assert(Fixture::seeks == sample + 1); // Stop before later samples or retries.
    assert(Fixture::reads == sample + (fault == Fixture::Fault::Seek ? 0 : 1));
  }
  ++cases;
}
int main(int argc, char** argv) {
  if (argc > 1) {
    const std::string mode = argv[1];
    const auto fault = mode == "seek" ? Fixture::Fault::Seek : mode == "negative" ? Fixture::Fault::Negative :
                       mode == "zero" ? Fixture::Fault::Zero : Fixture::Fault::Short;
    failed(8192, fault, 1);
    return 0;
  }
  for (size_t size : {size_t{0}, size_t{1}, size_t{1023}, size_t{1024}, size_t{1025}, size_t{4095},
                      size_t{4096}, size_t{4097}, size_t{16385}, size_t{1073742848}}) {
    healthy(size);
    failed(size, Fixture::Fault::Open, 0);
    healthy(size); // Ordinary open-error recovery.
    int sample = 0;
    for (size_t offset : sampleOffsets) {
      if (offset >= size) break;
      for (auto fault : {Fixture::Fault::Seek, Fixture::Fault::Negative, Fixture::Fault::Zero,
                         Fixture::Fault::Short, Fixture::Fault::Oversized}) {
        failed(size, fault, sample);
        failed(size, fault, sample); // Repeated errors retain no handle or digest.
        healthy(size); // A later call recovers the complete original identity input.
      }
      ++sample;
    }
  }
  for (const auto& path : {std::string{"/Books/name.epub"}, std::string{"name.epub"},
                           std::string{"/Books/\u00e9\u66f8.epub"}}) {
    resetCase(0, Fixture::Fault::Open);
    assert(KOReaderDocumentId::calculateFromFilename(path) == HashFixture::digest);
    const auto name = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
    assert(HashFixture::input == std::vector<uint8_t>(name.begin(), name.end()));
    assert(Fixture::opens == 0 && HashFixture::finalizes == 1 && HashFixture::publishes == 1);
    ++cases;
  }
  for (const auto& path : {std::string{}, std::string{"/Books/"}}) {
    resetCase(0);
    assert(KOReaderDocumentId::calculateFromFilename(path).empty());
    assert(Fixture::opens == 0 && HashFixture::begins == 0 && HashFixture::publishes == 0);
    ++cases;
  }
  for (auto fault : {Fixture::Fault::Open, Fixture::Fault::Seek, Fixture::Fault::Negative,
                     Fixture::Fault::Zero, Fixture::Fault::Short}) {
    Sync sync;
    for (int repetition = 0; repetition < 2; ++repetition) {
      resetCase(8192, fault, 1);
      sync.run();
      assert(sync.documentHash.empty() && sync.state == SYNC_FAILED);
      assert(sync.statusMessage == STR_HASH_FAILED && sync.networkCalls == 0);
      assert(sync.updates == repetition + 1 && Fixture::live == 0);
      ++cases;
    }
    resetCase(8192);
    sync.run();
    assert(sync.documentHash == HashFixture::digest && sync.networkCalls == 1);
    assert(Fixture::live == 0 && Fixture::closes == 1);
    ++cases;
  }
  resetCase(8192, Fixture::Fault::Open);
  KOREADER_STORE.method = DocumentMatchMethod::FILENAME;
  Sync filenameSync;
  filenameSync.run();
  assert(filenameSync.networkCalls == 1 && filenameSync.documentHash == HashFixture::digest);
  assert(Fixture::opens == 0); // Filename mode never requires content I/O.
  ++cases;
  std::cout << "KOReader document ID: " << cases << " production-source cases PASS\n";
}
