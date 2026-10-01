#include <RiscArchiveZipV1.h>
#include <RiscPlatformClockV1.h>
#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace RuntimeProviders;
namespace {
uint32_t contextGeneration = 0;
unsigned revoked = 0, closed = 0, yields = 0;
uint64_t clockMs = 0;
ModuleV2* interruptModule = nullptr;
bool openContext(risc_stream_provider_v1* out) {
  *out = {}; out->api_version = 1; out->struct_size = sizeof(*out);
  out->context = ++contextGeneration; return true;
}
void revoke(uint64_t) { ++revoked; }
void closeContext(uint64_t) { ++closed; }
const StreamHostV1 host{openContext, revoke, closeContext, nullptr, nullptr};
uint64_t now(void*) { return clockMs; }
void sleep(void*, uint32_t ms) {
  clockMs += ms; ++yields;
  if (interruptModule) {
    auto* module = interruptModule; interruptModule = nullptr;
    assert(!module->unload()); // in-flight call keeps ELF mapped
  }
}
const risc_platform_clock_api_v1 clockApi{1, sizeof(clockApi), nullptr, now, sleep};
std::vector<uint8_t> bytes(const char* path) {
  std::ifstream f(path, std::ios::binary); assert(f);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
uint64_t upload(const risc_archive_zip_api_v1* api, const std::vector<uint8_t>& source) {
  uint64_t job = 0; assert(api->begin(source.size(), &job) == RISC_ZIP_OK && job);
  for (size_t at = 0; at < source.size();) {
    const size_t count = std::min<size_t>(512, source.size() - at);
    assert(api->append(job, source.data() + at, count) == RISC_ZIP_OK); at += count;
  }
  return job;
}
}
int main(int argc, char** argv) {
  assert(argc == 10);
  const auto good = bytes(argv[3]);
  const RequirementV2 need{"platform.clock", 1};
  SpecV2 clock{"platform-clock-v1", argv[2], "platform.clock", 1, nullptr, 0};
  SpecV2 service{"archive-zip", argv[1], "archive.zip", 1, &need, 1};
  GraphV2 graph(&host);
  assert(graph.addVerified(clock) && graph.addVerified(service));
  auto lease = graph.acquire("archive.zip", 1); assert(lease.slot);
  auto* api = static_cast<const risc_archive_zip_api_v1*>(graph.interfaceFor(lease));
  assert(api && api->api_version == 1 && api->struct_size == sizeof(*api));
  auto submitted = good;
  uint64_t job = upload(api, submitted), refused = 99;
  std::fill(submitted.begin(), submitted.end(), 0); // service retains only its own copy
  assert(api->begin(good.size(), &refused) == RISC_ZIP_BUSY && !refused);
  uint32_t count = 99;
  assert(api->count(job, &count) == RISC_ZIP_INVALID && !count);
  assert(api->seal(job) == RISC_ZIP_OK);
  assert(api->count(job, &count) == RISC_ZIP_OK && count == 2);
  risc_archive_zip_entry_v1 entry{};
  assert(api->entry(job, 0, &entry) == RISC_ZIP_OK && !std::strcmp(entry.name, "assets/text.txt") && entry.size_bytes == 3);
  char out[4]{};
  assert(api->read(job, 0, 0, out, 3, &count) == RISC_ZIP_OK && count == 3 && !std::strcmp(out, "abc"));
  assert(api->entry(job, 1, &entry) == RISC_ZIP_OK && entry.size_bytes == 0);
  assert(api->read(job, 1, 0, nullptr, 0, &count) == RISC_ZIP_EOF && !count);
  assert(api->append(job, "x", 1) == RISC_ZIP_INVALID);
  assert(api->close(job + 1) == RISC_ZIP_STALE);
  // Releasing the capability closes an unfinished job, then a new provider
  // context cannot reuse its old token even if the same ELF address is reused.
  assert(graph.release(lease));
  lease = graph.acquire("archive.zip", 1); assert(lease.slot);
  api = static_cast<const risc_archive_zip_api_v1*>(graph.interfaceFor(lease));
  assert(api->close(job) == RISC_ZIP_STALE);
  job = upload(api, bytes(argv[4])); // ordinary valid empty ZIP
  assert(api->seal(job) == RISC_ZIP_OK);
  assert(api->count(job, &count) == RISC_ZIP_OK && !count);
  assert(api->close(job) == RISC_ZIP_OK);
  for (int i = 5; i < 9; ++i) {
    job = upload(api, bytes(argv[i]));
    assert(api->seal(job) < 0);
    assert(api->count(job, &count) < 0 && !count);
    assert(api->close(job) == RISC_ZIP_OK);
  }
  job = upload(api, bytes(argv[9]));
  assert(api->seal(job) == RISC_ZIP_OK);
  assert(api->read(job, 0, 120000 - 3, out, 3, &count) == RISC_ZIP_OK && count == 3);
  assert(out[0] == 'x' && out[1] == 'x' && out[2] == 'x');
  assert(api->close(job) == RISC_ZIP_OK);
  assert(graph.release(lease) && graph.shutdown());
  assert(revoked == closed);
  // Same actual loaded module with deterministic clock/deadline injection.
  ModuleV2 module; assert(module.setStreamHost(&host));
  const risc_provider_dependency_v1 dependency{"platform.clock", 1, &clockApi};
  assert(module.load(argv[1], "archive-zip", "archive.zip", 1, &dependency, 1));
  api = static_cast<const risc_archive_zip_api_v1*>(module.capability());
  assert(api->begin(RISC_ARCHIVE_ZIP_MAX_BYTES + 1, &job) == RISC_ZIP_LIMIT && !job);
  assert(api->begin(good.size(), &job) == RISC_ZIP_OK);
  assert(api->append(job, good.data(), 513) == RISC_ZIP_LIMIT);
  clockMs += 60000;
  assert(api->append(job, good.data(), 1) == RISC_ZIP_TIMEOUT);
  assert(api->close(job) == RISC_ZIP_OK); // expiry never prevents cleanup
  assert(api->begin(good.size(), &job) == RISC_ZIP_OK);
  clockMs += 60000;
  assert(api->begin(good.size(), &refused) == RISC_ZIP_OK && refused != job);
  assert(api->append(job, good.data(), 1) == RISC_ZIP_STALE);
  assert(api->close(refused) == RISC_ZIP_OK);
  job = upload(api, good); assert(api->seal(job) == RISC_ZIP_OK);
  assert(yields); clockMs = UINT64_MAX;
  assert(api->read(job, 0, 0, out, 3, &count) == RISC_ZIP_TIMEOUT && !count);
  clockMs = 120001; assert(api->close(job) == RISC_ZIP_OK);
  assert(module.unload());
  ModuleV2 interrupted; assert(interrupted.setStreamHost(&host));
  assert(interrupted.load(argv[1], "archive-zip", "archive.zip", 1, &dependency, 1));
  api = static_cast<const risc_archive_zip_api_v1*>(interrupted.capability());
  assert(api->begin(good.size(), &job) == RISC_ZIP_OK);
  interruptModule = &interrupted; clockMs += 8;
  assert(api->append(job, good.data(), 1) == RISC_ZIP_STALE);
  assert(interrupted.state() == ModuleV2::State::Failed);
  assert(api->begin(good.size(), &refused) == RISC_ZIP_BUSY);
  assert(interrupted.unload());
  puts("Loaded archive.zip: generic dependency/lease, stored files and empty ZIP, token generations, input/CRC/topology refusals and deadlines PASS");
}
