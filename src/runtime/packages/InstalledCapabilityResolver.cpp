#include "ProviderAbiProfile.h"
#include "InstalledCapabilityResolver.h"
#include "InstalledProviderRootScan.h"
#include "PackageOrdinaryManifest.h"
#include "PackageOrdinarySdAdapter.h"
#include "PackageOrdinaryStage.h"
#include <HalStorage.h>
#include <Logging.h>
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
// A healthy native-SD inventory can need over two seconds for bounded FAT
// directory/metadata reads. Match the storage operation's finite 15s envelope
// instead of discarding the entire capability snapshot at an arbitrary 2s.
// Entry, manifest, provider I/O and per-root time limits all remain enforced.
constexpr uint32_t kInventoryRootBudgetMs = 15000;

bool readSmall(const char* path, char* buffer, size_t capacity, size_t& length) {
  length = 0;
  if (!path || !buffer || capacity < 2) return false;
  HalFile file;
  if (!Storage.openFileForRead("CAPS", path, file)) return false;
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

struct InspectionStats {
  size_t rejected = 0;
  // Rejections describe existing reads, capped per cold scan so malformed
  // directories cannot turn diagnostics into unbounded serial traffic.
  void reject(const char* path, const char* stage, const char* entry = "", uint32_t detail = 0) {
    if (rejected++ < 12)
      LOG_ERR("CAPS", "Rejected provider metadata: path=%s stage=%s entry=%s detail=%lu",
              path, stage, entry, static_cast<unsigned long>(detail));
  }
};

// Inspect each candidate once per uncached operation. Quiescent snapshots may
// be reused only with unchanged observed storage epochs; never as content proof.
bool snapshotCandidates(std::vector<Candidate>& candidates, bool& reusable, InspectionStats& stats) {
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
    InstalledProviderRootScan scan;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    const TickType_t started = xTaskGetTickCount();
#endif
    while (true) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
      if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(kInventoryRootBudgetMs)) {
        LOG_ERR("CAPS", "Installed capability inventory timed out: root=%s entries=%u budget_ms=%lu",
                root, static_cast<unsigned>(examined),
                static_cast<unsigned long>(kInventoryRootBudgetMs));
        (void)directory.close();
        return false; // Never publish or retain a partial inventory.
      }
#endif
      // Metadata-only inspection still yields between bounded directory items.
      ordinaryCooperativeYield(1, 1);
      HalFile::DirectoryEntry item;
      if (!directory.readDirectoryEntry(item)) {
        if (directory.getError()) { (void)directory.close(); return false; }
        break;
      }
      ++examined;
      const char* id = item.name;
      const size_t n = std::strlen(id);
      const auto entry = scan.observe(id, item.isDirectory);
      if (entry == InstalledProviderRootScan::Entry::Exhausted) { (void)directory.close(); return false; }
      if (entry == InstalledProviderRootScan::Entry::CopyMetadata) continue;
      const bool candidate = item.isDirectory && n > 0 && n < sizeof(item.name) && safeId(id);
      if (!candidate) continue;
      char path[160]{};
      if (std::snprintf(path, sizeof(path), "%s/%s", root, id) >=
          static_cast<int>(sizeof(path))) continue;
      // Structural dependency resolution during integrity verification avoids
      // trusting a declaration merely because its own dependency claims it.
      Identity installed{};
      OrdinaryInspectionDiagnostic diagnostic;
      if (!inspectInstalledOrdinarySdDirectory(path, kPolicy,
              [](const char*) -> uint32_t { return UINT32_MAX; }, installed, plan.get(), &diagnostic)) {
        // A metadata refusal can be transient (for example allocation failure)
        // without a storage mutation. Never indefinitely cache that omission.
        reusable = false;
        stats.reject(path, diagnostic.stage, diagnostic.entry, diagnostic.detail);
        continue;
      }
      if (resourceOnly(installed)) continue;
      if (std::strcmp(installed.id, id) ||
          (installed.kind != Kind::Driver && installed.kind != Kind::Provider &&
           installed.kind != Kind::Service)) { reusable = false; stats.reject(path, "identity"); continue; }
      char profile[192]{};
      if (std::snprintf(profile, sizeof(profile), "%s/provider-abi.v1", path) >=
          static_cast<int>(sizeof(profile))) continue;
      char capability[64]{};
      uint32_t advertised = 0, osCpuAbi = 0;
      if (!parseProfile(profile, capability, advertised, osCpuAbi)) {
        reusable = false; stats.reject(path, "provider-profile"); continue;
      }
      char manifest[192]{};
      size_t length = 0;
      if (std::strcmp(plan->identity.id, id) || plan->identity.kind != installed.kind) {
        reusable = false; stats.reject(path, "plan-identity"); continue;
      }
      bool sourceDeclared=false;
      for(size_t i=0;i<plan->entryCount;++i)
        if(!std::strcmp(plan->entries[i].name,"manifest.json"))sourceDeclared=true;
      if(sourceDeclared) {
        uint32_t sourceRevision=0;
        if(std::snprintf(manifest,sizeof(manifest),"%s/manifest.json",path)>=static_cast<int>(sizeof(manifest)) ||
           !readSmall(manifest,json.get(),4097,length) ||
           !providerManifestOsCpuAbi(json.get(),length,sourceRevision) || sourceRevision!=osCpuAbi) {
          reusable=false;stats.reject(path, "source-abi");continue;
        }
      } else if(osCpuAbi!=1) { reusable=false;stats.reject(path, "source-abi-missing");continue; }
      bool declared = false;
      for (size_t i = 0; i < plan->entryCount; ++i)
        if (std::strcmp(plan->entries[i].name, "provider-abi.v1") == 0)
          declared = true;
      if (!declared) { reusable = false; stats.reject(path, "profile-undeclared"); continue; }
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

struct ResolutionFailure {
  const char* reason = nullptr;
  const char* capability = nullptr;
  const char* provider = nullptr;
  uint32_t required = 0;
  uint32_t available = 0;
};

// Graph evaluation only reads the bounded in-memory snapshot. Its recursive
// frame is a few indices and references, not SD handles, manifests or hash IO.
uint32_t resolveSnapshot(const char* capability, const std::vector<Candidate>& candidates,
                         size_t (&ancestry)[kMaxDepth], size_t depth, ResolutionFailure* failure) {
  if (!capability || !capability[0] || depth >= kMaxDepth) {
    if (failure) *failure = {"depth-or-input", capability, nullptr, 0, 0};
    return 0;
  }
  uint32_t best = 0;
  for (size_t index = 0; index < candidates.size(); ++index) {
    const Candidate& candidate = candidates[index];
    if (candidate.capability != capability || candidate.api <= best) continue;
    bool cycle = false;
    for (size_t i = 0; i < depth; ++i)
      if (candidate.id == candidates[ancestry[i]].id) { cycle = true; break; }
    if (cycle) {
      if (failure && !failure->reason) *failure = {"cycle", capability, candidate.id.c_str(), 0, 0};
      continue;
    }
    ancestry[depth] = index;
    bool ready = true;
    for (const OrdinaryRequirement& need : candidate.requirements) {
      ResolutionFailure dependency;
      const uint32_t available = resolveSnapshot(need.capability, candidates, ancestry, depth + 1,
                                                 failure ? &dependency : nullptr);
      if (available < need.minApi) {
        if (failure && !failure->reason) {
          *failure = dependency.reason ? dependency
              : ResolutionFailure{"required-api", need.capability, candidate.id.c_str(), need.minApi, available};
          if (!failure->provider) failure->provider = candidate.id.c_str();
          if (!failure->required) failure->required = need.minApi;
        }
        ready = false;
        break;
      }
    }
    if (ready) best = candidate.api;
  }
  if (!best && failure && !failure->reason)
    *failure = {"no-inspected-candidate", capability, nullptr, 0, 0};
  return best;
}
} // namespace

struct InstalledCapabilitySnapshot {
  std::shared_ptr<const std::vector<Candidate>> candidates;
  StorageGenerationStamp generation{};
  bool coherent = false;
  mutable unsigned failureLogs = 0;
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
  InspectionStats stats;
  const bool cached = snapshot->candidates != nullptr;
  if (!snapshot->candidates) {
    std::shared_ptr<std::vector<Candidate>> candidates(new (std::nothrow) std::vector<Candidate>());
    if (!candidates || !snapshotCandidates(*candidates, reusable, stats)) return nullptr;
    snapshot->candidates = candidates;
  }
  if (!cached)
    LOG_INF("CAPS", "Provider inventory inspected=%u rejected=%u reusable=%d quiescent=%d",
            static_cast<unsigned>(snapshot->candidates->size()), static_cast<unsigned>(stats.rejected),
            reusable ? 1 : 0, before.quiescent ? 1 : 0);
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
  if (!snapshot || !snapshot->candidates || !capability || !safePackageCapability(capability)) return 0;
  auto stale = [&]() {
    if (snapshot->failureLogs++ < 4)
      LOG_ERR("CAPS", "Capability lookup refused: capability=%s reason=storage-generation-changed", capability);
    return 0u;
  };
  if (snapshot->coherent && !Storage.unchanged(snapshot->generation)) return stale();
  size_t ancestry[kMaxDepth]{};
  ResolutionFailure failure;
  const uint32_t version = resolveSnapshot(capability, *snapshot->candidates, ancestry, 0, &failure);
  if (snapshot->coherent && !Storage.unchanged(snapshot->generation)) return stale();
  if (!version && snapshot->failureLogs++ < 4)
    LOG_ERR("CAPS", "Capability unresolved: requested=%s cause=%s dependency=%s provider=%s required=%lu available=%lu",
            capability, failure.reason ? failure.reason : "unknown",
            failure.capability ? failure.capability : capability, failure.provider ? failure.provider : "none",
            static_cast<unsigned long>(failure.required), static_cast<unsigned long>(failure.available));
  return version;
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
