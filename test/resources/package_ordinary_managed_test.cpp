// Reuse the exact four-kind production installer fixtures in this TU so the
// canonical manifest entrypoint tests the actual transaction rather than a
// parallel simulation of the engine.
#define main ordinary_installer_fixture_main
#include "package_ordinary_installer_test.cpp"
#undef main
#include "runtime/packages/PackageOrdinaryManagedInstall.h"

static std::string canonical(const OrdinaryPackagePlan& plan) {
  const char* kind = kindText(plan.identity.kind);
  std::string json = std::string("{\"schema\":1,\"kind\":\"") + kind +
      "\",\"id\":\"" + plan.identity.id +
      "\",\"version\":\"" + plan.identity.version +
      "\",\"artifact\":\"" + plan.identity.artifact +
      "\",\"architecture\":\"" + plan.architecture +
      "\",\"min_runtime_api\":1,\"entries\":[";
  for (size_t i = 0; i < plan.entryCount; ++i) {
    if (i) json += ',';
    const auto& item = plan.entries[i];
    json += std::string("{\"name\":\"") + item.name +
        "\",\"size_bytes\":" + std::to_string(item.sizeBytes) +
        ",\"sha256\":\"" + item.sha256 +
        "\",\"executable\":" + (item.executable ? "true" : "false") + "}";
  }
  return json + "],\"requires\":[]}";
}

struct RetainedDirectory {
  const Stage& stage;
  bool readManifest(char* out, size_t capacity, size_t& used) const {
    used = stage.manifest.size();
    if (!out || !used || used > capacity) return false;
    std::memcpy(out, stage.manifest.data(), used);
    return true;
  }
  bool exactEntries(const OrdinaryPackagePlan& plan) const {
    if (stage.manifest.empty() || stage.files.size() != plan.entryCount) return false;
    for (size_t i = 0; i < plan.entryCount; ++i)
      if (stage.files.count(plan.entries[i].name) != 1) return false;
    return true;
  }
  bool entrySize(const char* name, uint64_t& length) const {
    const auto found = stage.files.find(name);
    if (found == stage.files.end()) return false;
    length = found->second.size();
    return true;
  }
  bool readAt(const char* name, uint64_t offset, uint8_t* data, size_t length) const {
    const auto found = stage.files.find(name);
    if (found == stage.files.end() || offset > found->second.size() ||
        length > found->second.size() - static_cast<size_t>(offset)) return false;
    std::memcpy(data, found->second.data() + offset, length);
    return true;
  }
};

int main() {
  for (Kind kind : {Kind::Application, Kind::Driver, Kind::Service, Kind::Provider}) {
    Source source = makeSource();
    const OrdinaryPackagePlan candidate = makePlan(source, kind);
    const std::string manifest = canonical(candidate);
    Storage disk;
    Stage stage(disk);
    Hash hash;
    uint8_t io[kOrdinaryIoBytes]{};
    const auto installed = installCanonicalOrdinaryPackage(manifest.data(),
        manifest.size(), source, stage, hash, resolver, kPolicy, io, disk,
        [&disk](const char* path, Identity& observed) { return disk.verify(path, observed); },
        [&disk](const char* path) { return disk.purge(path); }, true);
    assert(installed.result == OrdinaryInstallResult::Installed);
    RetainedDirectory retained{stage};
    Identity verified{};
    assert(verifyCanonicalOrdinaryDirectory(retained, hash, resolver,
                                           kPolicy, io, verified));
    assert(samePackage(candidate.identity, verified) &&
           std::strcmp(candidate.identity.version, verified.version) == 0);
    stage.files["module.elf"][35] ^= 1;
    assert(!verifyCanonicalOrdinaryDirectory(retained, hash, resolver,
                                            kPolicy, io, verified));
    assert(!verified.id[0]);
    stage.files["module.elf"][35] ^= 1;
    stage.files["unexpected.txt"] = {1};
    assert(!verifyCanonicalOrdinaryDirectory(retained, hash, resolver,
                                            kPolicy, io, verified));
    stage.files.erase("unexpected.txt");
    stage.manifest[0] = '[';
    assert(!verifyCanonicalOrdinaryDirectory(retained, hash, resolver,
                                            kPolicy, io, verified));
  }
  // Invalid metadata must reject without beginning, cleaning or touching an
  // existing transaction generation. The driver was not activated.
  Source source = makeSource();
  const auto candidate = makePlan(source, Kind::Driver);
  Storage disk;
  Stage stage(disk);
  const auto p = paths(Kind::Driver);
  disk.directories[p.target] = {candidate.identity, true};
  std::string invalid = canonical(candidate) + " trailing";
  Hash hash;
  uint8_t io[kOrdinaryIoBytes]{};
  const auto refused = installCanonicalOrdinaryPackage(invalid.data(),
      invalid.size(), source, stage, hash, resolver, kPolicy, io, disk,
      [&disk](const char* path, Identity& observed) { return disk.verify(path, observed); },
      [&disk](const char* path) { return disk.purge(path); }, true);
  assert(refused.result == OrdinaryInstallResult::InvalidInput);
  assert(disk.exists(p.target) && !disk.exists(p.stage) && !stage.ownsStage);
  std::puts("Canonical four-kind install: parsed declarations, typed publication, retained-manifest reparse, SHA-256 and exact inventory PASS");
}
