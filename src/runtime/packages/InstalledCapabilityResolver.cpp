#include "InstalledCapabilityResolver.h"
#include "PackageOrdinaryManifest.h"
#include "PackageOrdinarySdAdapter.h"
#include <HalStorage.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace RuntimePackages {
namespace {
constexpr PackageRuntimePolicy kPolicy{
    "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
constexpr size_t kMaxDepth = 8;
constexpr size_t kMaxEntriesPerRoot = 64;

bool readSmall(const char* path, char* buffer, size_t capacity, size_t& length) {
  length = 0;
  if (!path || !buffer || capacity < 2) return false;
  HalFile file = Storage.open(path, O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  const uint64_t size = file.fileSize64();
  if (!size || size >= capacity) {
    (void)file.close();
    return false;
  }
  length = static_cast<size_t>(size);
  const bool read = file.read(reinterpret_cast<uint8_t*>(buffer), length) ==
                    static_cast<int>(length);
  const bool closed = file.close();
  if (!read || !closed) return false;
  buffer[length] = '\0';
  return true;
}

// A provider profile counts only after the entire managed directory has been
// verified against its exact inventory and per-entry hashes. A profile is not
// a grant of execution or hardware rights.
bool parseProfile(const char* path, char (&capability)[64], uint32_t& version) {
  version = 0;
  capability[0] = '\0';
  char bytes[192]{};
  size_t length = 0;
  if (!readSmall(path, bytes, sizeof(bytes), length)) return false;
  const char prefix[] = "os-cpu-abi=1\nprovides=";
  if (std::strncmp(bytes, prefix, sizeof(prefix) - 1)) return false;
  const char* declared = bytes + sizeof(prefix) - 1;
  const char* end = std::strchr(declared, '\n');
  if (!end || end == declared || static_cast<size_t>(end - declared) >= sizeof(capability) ||
      std::strncmp(end, "\napi=", 5)) return false;
  std::memcpy(capability, declared, static_cast<size_t>(end - declared));
  capability[end - declared] = '\0';
  if (!safePackageCapability(capability)) return false;
  const char* number = end + 5;
  if (*number < '1' || *number > '9') return false;
  char* tail = nullptr;
  const unsigned long parsed = std::strtoul(number, &tail, 10);
  if (!tail || tail == number || *tail != '\n' || tail[1] != '\0' ||
      !parsed || parsed > UINT32_MAX) return false;
  version = static_cast<uint32_t>(parsed);
  return true;
}

struct Candidate {
  std::string id;
  std::string capability;
  uint32_t api = 0;
  std::vector<OrdinaryRequirement> requirements;
};

// Snapshot and hash each candidate ONCE per query. The old recursive resolver
// opened all three directories and rehashed every candidate at every dependency
// level, retaining directory/manifest/identity workspaces in recursive frames.
// That caused multi-minute UI stalls and overflowed loopTask on deep graphs.
// This snapshot never survives the call: a later query independently verifies
// current on-card bytes rather than trusting a stale global cache.
bool snapshotCandidates(std::vector<Candidate>& candidates) {
  std::unique_ptr<char[]> json(new (std::nothrow) char[4097]{});
  std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
  if (!json || !plan) return false;
  const char* const roots[] = {"/Drivers", "/Providers", "/Services"};
  for (const char* root : roots) {
    HalFile directory = Storage.open(root, O_RDONLY);
    if (!directory.isOpen() || !directory.isDirectory()) {
      if (directory.isOpen()) (void)directory.close();
      continue;
    }
    size_t examined = 0;
    while (examined++ < kMaxEntriesPerRoot) {
      HalFile item = directory.openNextFile();
      if (!item.isOpen()) break;
      char id[64]{};
      const size_t n = item.getName(id, sizeof(id));
      const bool candidate = item.isDirectory() && n > 0 && n < sizeof(id) && safeId(id);
      (void)item.close();
      if (!candidate) continue;
      char path[160]{};
      if (std::snprintf(path, sizeof(path), "%s/%s", root, id) >=
          static_cast<int>(sizeof(path))) continue;
      // Structural dependency resolution during integrity verification avoids
      // trusting a declaration merely because its own dependency claims it.
      Identity installed{};
      if (!verifyOrdinarySdDirectory(path, kPolicy,
              [](const char*) -> uint32_t { return UINT32_MAX; }, installed) ||
          std::strcmp(installed.id, id) ||
          (installed.kind != Kind::Driver && installed.kind != Kind::Provider &&
           installed.kind != Kind::Service)) continue;
      char profile[192]{};
      if (std::snprintf(profile, sizeof(profile), "%s/provider-abi.v1", path) >=
          static_cast<int>(sizeof(profile))) continue;
      char capability[64]{};
      uint32_t advertised = 0;
      if (!parseProfile(profile, capability, advertised)) continue;
      char manifest[192]{};
      if (std::snprintf(manifest, sizeof(manifest), "%s/.package.json", path) >=
          static_cast<int>(sizeof(manifest))) continue;
      size_t length = 0;
      *plan = OrdinaryPackagePlan{};
      if (!readSmall(manifest, json.get(), 4097, length) ||
          !parseOrdinaryManifest(json.get(), length, *plan) ||
          std::strcmp(plan->identity.id, id) ||
          plan->identity.kind != installed.kind) continue;
      bool declared = false;
      for (size_t i = 0; i < plan->entryCount; ++i)
        if (std::strcmp(plan->entries[i].name, "provider-abi.v1") == 0)
          declared = true;
      if (!declared) continue;
      Candidate provider;
      provider.id = id;
      provider.capability = capability;
      provider.api = advertised;
      provider.requirements.assign(plan->requirements,
                                   plan->requirements + plan->requirementCount);
      candidates.push_back(std::move(provider));
    }
    if (!directory.close()) return false;
  }
  return true;
}

// Graph evaluation only reads the bounded in-memory snapshot. Its recursive
// frame is a few indices and references, not SD handles, manifests or hash IO.
uint32_t resolveSnapshot(const char* capability, const std::vector<Candidate>& candidates,
                         size_t (&ancestry)[kMaxDepth], size_t depth) {
  if (!capability || !capability[0] || depth >= kMaxDepth) return 0;
  uint32_t best = 0;
  for (size_t index = 0; index < candidates.size(); ++index) {
    const Candidate& candidate = candidates[index];
    if (candidate.capability != capability || candidate.api <= best) continue;
    bool cycle = false;
    for (size_t i = 0; i < depth; ++i)
      if (candidate.id == candidates[ancestry[i]].id) { cycle = true; break; }
    if (cycle) continue;
    ancestry[depth] = index;
    bool ready = true;
    for (const OrdinaryRequirement& need : candidate.requirements) {
      if (resolveSnapshot(need.capability, candidates, ancestry, depth + 1) < need.minApi) {
        ready = false;
        break;
      }
    }
    if (ready) best = candidate.api;
  }
  return best;
}
} // namespace

uint32_t installedCapabilityVersion(const char* capability) {
  if (!capability || !safePackageCapability(capability) || !Storage.ready()) return 0;
  std::vector<Candidate> candidates;
  if (!snapshotCandidates(candidates)) return 0;
  size_t ancestry[kMaxDepth]{};
  return resolveSnapshot(capability, candidates, ancestry, 0);
}
} // namespace RuntimePackages
