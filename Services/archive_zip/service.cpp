// Software-only installed capability. No firmware ZIP proxy, filesystem path,
// retained caller pointer, hardware import or second package installer.
#include <RiscArchiveZipV1.h>
#include <RiscPlatformClockV1.h>
#include <RiscProviderV2.h>
#include "runtime/packages/PackageRteZip.h"
#include <cstring>
#include "Admission.h"

namespace {
using namespace RuntimePackages;
uint8_t bytes[RISC_ARCHIVE_ZIP_MAX_BYTES];
RteZipView archive{};
const risc_platform_clock_api_v1* clockApi = nullptr;
uint32_t generation = 0, nextJob = 0, expected = 0, received = 0, work = 0;
uint64_t job = 0, started = 0, lastYield = 0;
bool running = false, sealed = false;
ArchiveAdmission admission;
int32_t failure = RISC_ZIP_OK;
constexpr uint64_t kTimeoutMs = 60000;
uint64_t now() { return clockApi ? clockApi->monotonic_ms(clockApi->context) : UINT64_MAX; }
int32_t checkpoint() {
  if (admission.stopping()) return failure = RISC_ZIP_STALE;
  const uint64_t time = now();
  if (time == UINT64_MAX || time < started || time - started >= kTimeoutMs)
    return failure = RISC_ZIP_TIMEOUT;
  if (++work >= 8 || time - lastYield >= 8) {
    clockApi->sleep_ms(clockApi->context, 1);
    lastYield = now(); work = 0;
    if (admission.stopping()) return failure = RISC_ZIP_STALE;
    if (lastYield == UINT64_MAX || lastYield < started || lastYield - started >= kTimeoutMs)
      return failure = RISC_ZIP_TIMEOUT;
  }
  return failure;
}
struct Guard {
  bool locked;
  Guard() : locked(admission.enter()) {}
  ~Guard() { if (locked) admission.leave(); }
};
struct Call : Guard {
  bool admitted;
  Call() : admitted(locked && running && !admission.stopping()) {}
};
int32_t valid(uint64_t token, bool requireSealed) {
  if (!job || token != job) return RISC_ZIP_STALE;
  const auto state = checkpoint();
  if (state != RISC_ZIP_OK) return state;
  return requireSealed && !sealed ? RISC_ZIP_INVALID : RISC_ZIP_OK;
}
void clear() {
  // Explicit finite cleanup, including timed-out or incomplete input. No caller
  // callbacks or I/O; keep the clock dependency pinned through quiescence.
  for (uint32_t at = 0; at < received; at += RISC_ARCHIVE_ZIP_CHUNK) {
    const uint32_t count = received - at < RISC_ARCHIVE_ZIP_CHUNK ? received - at : RISC_ARCHIVE_ZIP_CHUNK;
    std::memset(bytes + at, 0, count);
    if (clockApi && (at & 4095u) == 0) clockApi->sleep_ms(clockApi->context, 1);
  }
  archive = {}; job = 0; received = expected = 0; sealed = false; failure = RISC_ZIP_OK;
}
int32_t begin(uint32_t size, uint64_t* out) {
  if (out) *out = 0;
  Call call; if (!call.admitted) return RISC_ZIP_BUSY;
  if (!out || size < kRteZipEocdBytes) return RISC_ZIP_INVALID;
  if (size > sizeof(bytes)) return RISC_ZIP_LIMIT;
  if (job) {
    const uint64_t time = now();
    if (time == UINT64_MAX || time < started || time - started >= kTimeoutMs) clear();
    else return RISC_ZIP_BUSY;
  }
  if (!generation || nextJob == UINT32_MAX) return RISC_ZIP_LIMIT;
  started = lastYield = now();
  if (started == UINT64_MAX) return RISC_ZIP_TIMEOUT;
  work = 0; expected = size; received = 0; sealed = false; failure = RISC_ZIP_OK;
  job = (static_cast<uint64_t>(generation) << 32) | ++nextJob;
  *out = job; return RISC_ZIP_OK;
}
int32_t append(uint64_t token, const void* data, uint32_t size) {
  Call call; if (!call.admitted) return RISC_ZIP_BUSY;
  const auto state = valid(token, false); if (state) return state;
  if (sealed || !data || !size) return RISC_ZIP_INVALID;
  if (size > RISC_ARCHIVE_ZIP_CHUNK || size > expected - received) return RISC_ZIP_LIMIT;
  std::memcpy(bytes + received, data, size); received += size;
  return checkpoint();
}
int32_t seal(uint64_t token) {
  Call call; if (!call.admitted) return RISC_ZIP_BUSY;
  const auto state = valid(token, false); if (state) return state;
  if (sealed || received != expected) return RISC_ZIP_INVALID;
  auto readAt = [](uint64_t at, uint8_t* out, size_t count) {
    if (checkpoint() || !out || at > received || count > received - at) return false;
    if (count) std::memcpy(out, bytes + at, count);
    return true;
  };
  const auto inspected = inspectRteZip(readAt, received, archive, false);
  if (failure) return failure;
  if (inspected != RteZipResult::Ready) {
    failure = inspected == RteZipResult::UnsupportedFeature ? RISC_ZIP_UNSUPPORTED : RISC_ZIP_CORRUPT;
    return failure;
  }
  for (uint16_t i = 0; i < archive.entryCount; ++i) {
    const char* name = archive.entries[i].name;
    if (std::strcmp(name, kOrdinaryManifestName) && !safePackageResourcePath(name))
      return failure = RISC_ZIP_UNSUPPORTED;
    for (uint16_t j = 0; j < i; ++j)
      if (packagePathIsParent(name, archive.entries[j].name) ||
          packagePathIsParent(archive.entries[j].name, name)) return failure = RISC_ZIP_CORRUPT;
  }
  if (!RteZipInstallDetail::validateArchive(readAt, archive, false))
    return failure ? failure : (failure = RISC_ZIP_CORRUPT);
  if (checkpoint()) return failure;
  sealed = true; return RISC_ZIP_OK;
}
int32_t count(uint64_t token, uint32_t* out) {
  if (out) *out = 0;
  Call call; if (!call.admitted) return RISC_ZIP_BUSY;
  const auto state = valid(token, true); if (state) return state;
  if (!out) return RISC_ZIP_INVALID;
  *out = archive.entryCount; return RISC_ZIP_OK;
}
int32_t entry(uint64_t token, uint32_t index, risc_archive_zip_entry_v1* out) {
  if (out) *out = {};
  Call call; if (!call.admitted) return RISC_ZIP_BUSY;
  const auto state = valid(token, true); if (state) return state;
  if (!out || index >= archive.entryCount) return RISC_ZIP_INVALID;
  std::memcpy(out->name, archive.entries[index].name, sizeof(out->name));
  out->size_bytes = archive.entries[index].sizeBytes; return RISC_ZIP_OK;
}
int32_t read(uint64_t token, uint32_t index, uint32_t offset, void* out, uint32_t cap, uint32_t* size) {
  if (size) *size = 0;
  Call call; if (!call.admitted) return RISC_ZIP_BUSY;
  const auto state = valid(token, true); if (state) return state;
  if (!size || index >= archive.entryCount || cap > RISC_ARCHIVE_ZIP_CHUNK) return RISC_ZIP_INVALID;
  const auto& item = archive.entries[index];
  if (offset > item.sizeBytes) return RISC_ZIP_INVALID;
  if (offset == item.sizeBytes) return RISC_ZIP_EOF; // includes valid empty files
  if (!out || !cap) return RISC_ZIP_INVALID;
  const uint32_t amount = item.sizeBytes - offset < cap ? item.sizeBytes - offset : cap;
  std::memcpy(out, bytes + item.dataOffset + offset, amount); *size = amount;
  return RISC_ZIP_OK;
}
int32_t close(uint64_t token) {
  Call call; if (!call.admitted) return RISC_ZIP_BUSY;
  if (!job || token != job) return RISC_ZIP_STALE;
  clear(); return RISC_ZIP_OK;
}
bool bind(const risc_stream_provider_v1* host) {
  if (!admission.prepare()) return false;
  Guard guard; if (!guard.locked) return false;
  if (running || !host || host->api_version != RISC_STREAM_PROVIDER_API_V1 ||
      host->struct_size < sizeof(*host) || !host->context || host->context > UINT32_MAX) return false;
  generation = static_cast<uint32_t>(host->context); nextJob = 0; return true;
}
bool start(const risc_provider_dependency_v1* deps, size_t count) {
  Guard guard; if (!guard.locked) return false;
  if (running || !generation || !deps || count != 1 || !deps[0].capability_id ||
      std::strcmp(deps[0].capability_id, "platform.clock") || deps[0].api_version != 1 || !deps[0].api) return false;
  const auto* candidate = static_cast<const risc_platform_clock_api_v1*>(deps[0].api);
  if (candidate->api_version != 1 || candidate->struct_size < sizeof(*candidate) ||
      !candidate->monotonic_ms || !candidate->sleep_ms) return false;
  clockApi = candidate;
  if (now() == UINT64_MAX) { clockApi = nullptr; return false; }
  clear(); admission.restart(); running = true; return true;
}
bool quiesce() {
  if (!admission.ready()) return true; // failed admission allocation owns no job
  admission.requestStop();
  Guard guard; if (!guard.locked) return false;
  running = false; clear(); return true;
}
void stop() { running = false; generation = 0; clockApi = nullptr; admission.destroy(); }
const risc_archive_zip_api_v1 api{1, sizeof(api), begin, append, seal, count, entry, read, close};
const risc_driver_streams_v2 service{
  {RISC_PROVIDER_DRIVER_ABI_V2, sizeof(service), "archive-zip", "archive.zip", 1,
   &api, start, stop, quiesce}, nullptr, bind
};
}
extern "C" __attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t abi) { return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &service.driver : nullptr; }
