#include "ProviderAbiProfile.h"
#include "InstalledCapabilityResolver.h"
#include "PackageOrdinaryManifest.h"
#include "PackageOrdinarySdAdapter.h"
#include "PackageOrdinaryStage.h"
#include <HalStorage.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace RuntimePackages {
namespace {
constexpr PackageRuntimePolicy kPolicy{
    "xtensa-esp32s3", 2, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
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

// A provider profile counts only after bounded installed-directory metadata
// inspection. This hot path does not rehash contents. A profile is not
// a grant of execution or hardware rights.
bool parseProfile(const char* path, char (&capability)[64], uint32_t& version, uint32_t& revision) {
  version = 0;
  capability[0] = '\0';
  char bytes[192]{};
  size_t length = 0;
  if (!readSmall(path, bytes, sizeof(bytes), length)) return false;
  return parseProviderAbiProfile(bytes,length,revision,capability,version);
}

struct Candidate {
  std::string id;
  std::string capability;
  uint32_t api = 0;
  std::vector<OrdinaryRequirement> requirements;
};

// Inspect each candidate once per uncached operation. Quiescent snapshots may
// be reused only with unchanged observed storage epochs; never as content proof.
bool snapshotCandidates(std::vector<Candidate>& candidates, bool& reusable) {
  reusable = true;
  std::unique_ptr<char[]> json(new (std::nothrow) char[4097]{});
  std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
  if (!json || !plan) return false;
  const char* const roots[] = {"/Drivers", "/Providers", "/Services"};
  for (const char* root : roots) {
    HalFile directory = Storage.open(root, O_RDONLY);
    if (!directory.isOpen()) {
      if (Storage.exists(root)) return false;
      continue; // Optional namespace absent; no content-integrity claim.
    }
    if (!directory.isDirectory()) { (void)directory.close(); return false; }
    size_t examined = 0;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    const TickType_t started = xTaskGetTickCount();
#endif
    while (true) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
      if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(2000)) { (void)directory.close(); return false; }
#endif
      // Metadata-only inspection still yields between bounded directory items.
      ordinaryCooperativeYield(1, 1);
      HalFile item = directory.openNextFile();
      if (!item.isOpen()) {
        if (directory.getError()) { (void)directory.close(); return false; }
        break;
      }
      if (examined++ == kMaxEntriesPerRoot) {
        (void)item.close(); (void)directory.close(); return false;
      }
      char id[64]{};
      const size_t n = item.getName(id, sizeof(id));
      const bool candidate = item.isDirectory() && n > 0 && n < sizeof(id) && safeId(id);
      if (!item.close()) { (void)directory.close(); return false; }
      if (!candidate) continue;
      char path[160]{};
      if (std::snprintf(path, sizeof(path), "%s/%s", root, id) >=
          static_cast<int>(sizeof(path))) continue;
      // Structural dependency resolution during integrity verification avoids
      // trusting a declaration merely because its own dependency claims it.
      Identity installed{};
      if (!inspectInstalledOrdinarySdDirectory(path, kPolicy,
              [](const char*) -> uint32_t { return UINT32_MAX; }, installed)) {
        // A metadata refusal can be transient (for example allocation failure)
        // without a storage mutation. Never indefinitely cache that omission.
        reusable = false;
        continue;
      }
      if (resourceOnly(installed)) continue;
      if (std::strcmp(installed.id, id) ||
          (installed.kind != Kind::Driver && installed.kind != Kind::Provider &&
           installed.kind != Kind::Service)) { reusable = false; continue; }
      char profile[192]{};
      if (std::snprintf(profile, sizeof(profile), "%s/provider-abi.v1", path) >=
          static_cast<int>(sizeof(profile))) continue;
      char capability[64]{};
      uint32_t advertised = 0, osCpuAbi = 0;
      if (!parseProfile(profile, capability, advertised, osCpuAbi)) { reusable = false; continue; }
      char manifest[192]{};
      if (std::snprintf(manifest, sizeof(manifest), "%s/.package.json", path) >=
          static_cast<int>(sizeof(manifest))) continue;
      size_t length = 0;
      *plan = OrdinaryPackagePlan{};
      if (!readSmall(manifest, json.get(), 4097, length) ||
          !parseOrdinaryManifest(json.get(), length, *plan) ||
          std::strcmp(plan->identity.id, id) ||
          plan->identity.kind != installed.kind) { reusable = false; continue; }
      bool sourceDeclared=false;
      for(size_t i=0;i<plan->entryCount;++i)
        if(!std::strcmp(plan->entries[i].name,"manifest.json"))sourceDeclared=true;
      if(sourceDeclared) {
        uint32_t sourceRevision=0;
        if(std::snprintf(manifest,sizeof(manifest),"%s/manifest.json",path)>=static_cast<int>(sizeof(manifest)) ||
           !readSmall(manifest,json.get(),4097,length) ||
           !providerManifestOsCpuAbi(json.get(),length,sourceRevision) || sourceRevision!=osCpuAbi) {
          reusable=false;continue;
        }
      } else if(osCpuAbi!=1) { reusable=false;continue; }
      bool declared = false;
      for (size_t i = 0; i < plan->entryCount; ++i)
        if (std::strcmp(plan->entries[i].name, "provider-abi.v1") == 0)
          declared = true;
      if (!declared) { reusable = false; continue; }
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

struct InstalledCapabilitySnapshot {
  std::shared_ptr<const std::vector<Candidate>> candidates;
  StorageGenerationStamp generation{};
  bool coherent = false;
};
namespace {
std::mutex inventoryMutex;
std::shared_ptr<const std::vector<Candidate>> retainedCandidates;
StorageGenerationStamp retainedGeneration{};
}

InstalledCapabilitySnapshot* captureInstalledCapabilities() {
  if (!Storage.ready()) return nullptr;
  const auto before = Storage.generation();
  std::unique_ptr<InstalledCapabilitySnapshot> snapshot(new (std::nothrow) InstalledCapabilitySnapshot());
  if (!snapshot) return nullptr;
  if (before.quiescent) {
    // Only pointer/stamp ownership is shared under this lock; no filesystem IO.
    std::lock_guard<std::mutex> lock(inventoryMutex);
    if (before.matches(retainedGeneration)) snapshot->candidates = retainedCandidates;
    else retainedCandidates.reset();
  }
  bool reusable = true;
  if (!snapshot->candidates) {
    std::shared_ptr<std::vector<Candidate>> candidates(new (std::nothrow) std::vector<Candidate>());
    if (!candidates || !snapshotCandidates(*candidates, reusable)) return nullptr;
    snapshot->candidates = candidates;
  }
  // Compatible mutable raw-storage callers retain operation-local metadata
  // lookup, without entering the coherent cache or granting execution rights.
  if (!before.quiescent) return snapshot.release();
  if (!Storage.unchanged(before)) return nullptr;
  snapshot->generation = before;
  snapshot->coherent = true;
  if (reusable) {
    std::lock_guard<std::mutex> lock(inventoryMutex);
    retainedCandidates = snapshot->candidates;
    retainedGeneration = before;
  }
  return snapshot.release();
}

uint32_t versionInInstalledSnapshot(const InstalledCapabilitySnapshot* snapshot,
                                    const char* capability) {
  if (!snapshot || !snapshot->candidates || !capability || !safePackageCapability(capability) ||
      (snapshot->coherent && !Storage.unchanged(snapshot->generation))) return 0;
  size_t ancestry[kMaxDepth]{};
  const uint32_t version = resolveSnapshot(capability, *snapshot->candidates, ancestry, 0);
  return snapshot->coherent && !Storage.unchanged(snapshot->generation) ? 0 : version;
}

void releaseInstalledCapabilities(InstalledCapabilitySnapshot* snapshot) {
  delete snapshot;
}

uint32_t installedCapabilityVersion(const char* capability) {
  if (!capability || !safePackageCapability(capability) || !Storage.ready()) return 0;
  InstalledCapabilitySnapshot* snapshot = captureInstalledCapabilities();
  if (!snapshot) return 0;
  const uint32_t result = versionInInstalledSnapshot(snapshot, capability);
  releaseInstalledCapabilities(snapshot);
  return result;
}
} // namespace RuntimePackages
