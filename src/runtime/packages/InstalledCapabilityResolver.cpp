#include "InstalledCapabilityResolver.h"
#include "PackageOrdinaryManifest.h"
#include "PackageOrdinarySdAdapter.h"
#include <HalStorage.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

// This metadata must be an entry in .package.json. The exact-inventory and
// per-entry digest verification below prevent an undeclared file from being
// treated as an installed capability. This check says nothing about authority.
bool parseProfile(const char* path, const char* requested, uint32_t& version) {
  version = 0;
  char bytes[192]{};
  size_t length = 0;
  if (!readSmall(path, bytes, sizeof(bytes), length)) return false;
  const char prefix[] = "os-cpu-abi=1\nprovides=";
  if (std::strncmp(bytes, prefix, sizeof(prefix) - 1)) return false;
  const char* capability = bytes + sizeof(prefix) - 1;
  const char* end = std::strchr(capability, '\n');
  if (!end || end == capability || static_cast<size_t>(end - capability) >= 64 ||
      static_cast<size_t>(end - capability) != std::strlen(requested) ||
      std::memcmp(capability, requested, end - capability) ||
      std::strncmp(end, "\napi=", 5)) return false;
  const char* number = end + 5;
  if (*number < '1' || *number > '9') return false;
  char* tail = nullptr;
  const unsigned long parsed = std::strtoul(number, &tail, 10);
  if (!tail || tail == number || *tail != '\n' || tail[1] != '\0' ||
      !parsed || parsed > UINT32_MAX) return false;
  version = static_cast<uint32_t>(parsed);
  return true;
}

uint32_t resolve(const char* capability, char (&stack)[kMaxDepth][64],
                 size_t depth) {
  if (!capability || !capability[0] || std::strlen(capability) >= 64 ||
      depth >= kMaxDepth || !Storage.ready()) return 0;
  const char* const roots[] = {"/Drivers", "/Providers", "/Services"};
  uint32_t best = 0;
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
      const bool candidate = item.isDirectory() && n > 0 && n < sizeof(id) &&
                             safeId(id);
      (void)item.close();
      if (!candidate) continue;
      bool cycle = false;
      for (size_t i = 0; i < depth; ++i) {
        if (std::strcmp(stack[i], id) == 0) { cycle = true; break; }
      }
      if (cycle) continue;
      char path[160]{};
      if (std::snprintf(path, sizeof(path), "%s/%s", root, id) >=
          static_cast<int>(sizeof(path))) continue;
      // Verify bytes and the EXACT declared inventory first. Using a structural
      // resolver here avoids circularly assuming that a dependency is already
      // active merely because its own manifest declares it.
      Identity installed{};
      if (!verifyOrdinarySdDirectory(path, kPolicy,
              [](const char*) -> uint32_t { return UINT32_MAX; }, installed) ||
          std::strcmp(installed.id, id) ||
          (installed.kind != Kind::Driver && installed.kind != Kind::Provider &&
           installed.kind != Kind::Service)) continue;
      char profile[192]{};
      if (std::snprintf(profile, sizeof(profile), "%s/provider-abi.v1", path) >=
          static_cast<int>(sizeof(profile))) continue;
      uint32_t advertised = 0;
      if (!parseProfile(profile, capability, advertised) || advertised <= best)
        continue;
      char manifest[192]{};
      if (std::snprintf(manifest, sizeof(manifest), "%s/.package.json", path) >=
          static_cast<int>(sizeof(manifest))) continue;
      char json[4097]{};
      size_t length = 0;
      OrdinaryPackagePlan plan{};
      if (!readSmall(manifest, json, sizeof(json), length) ||
          !parseOrdinaryManifest(json, length, plan) ||
          std::strcmp(plan.identity.id, id) ||
          plan.identity.kind != installed.kind) continue;
      bool declared = false;
      for (size_t i = 0; i < plan.entryCount; ++i)
        if (std::strcmp(plan.entries[i].name, "provider-abi.v1") == 0)
          declared = true;
      if (!declared) continue;
      std::strcpy(stack[depth], id);
      bool dependenciesReady = true;
      for (size_t i = 0; i < plan.requirementCount; ++i) {
        const auto& need = plan.requirements[i];
        if (resolve(need.capability, stack, depth + 1) < need.minApi) {
          dependenciesReady = false;
          break;
        }
      }
      stack[depth][0] = '\0';
      if (dependenciesReady) best = advertised;
    }
    (void)directory.close();
  }
  return best;
}
} // namespace

uint32_t installedCapabilityVersion(const char* capability) {
  char stack[kMaxDepth][64]{};
  return resolve(capability, stack, 0);
}
} // namespace RuntimePackages
