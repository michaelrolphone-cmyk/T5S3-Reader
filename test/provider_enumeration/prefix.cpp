// Compile the unchanged production enumerator and read/profile helpers against
// real HalStorage generation/handle ownership and the existing fault-media fake.
// The manifest-inspection edge below is deterministic; executable admission,
// hashing and activation are deliberately outside this metadata-cache test.
#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <SdFat.h>
#include <Arduino.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageCdcSdMigration.h"
#include "runtime/packages/PackageOrdinaryStage.h"
#include "runtime/packages/ProviderAbiProfile.h"
#include "runtime/packages/PackageIdentity.h"
namespace RuntimePackages {
unsigned inspections = 0;
bool cdcMigrationPendingOnSd() { return false; } // No CDC lineage in fixture.
bool inspectInstalledOrdinarySdDirectory(const char* path, const PackageRuntimePolicy&,
    uint32_t (*)(const char*), Identity& out) {
  ++inspections;
  HalFile file = Storage.open((std::string(path) + "/.package.json").c_str());
  if (!file.isOpen()) return false;
  char data[8]{};
  const bool okay = file.read(data, sizeof(data)) == 5 && !std::strcmp(data, "valid");
  const bool closed = file.close();
  const Kind kind = !std::strncmp(path, "/Drivers/", 9) ? Kind::Driver :
                    !std::strncmp(path, "/Providers/", 11) ? Kind::Provider : Kind::Service;
  return okay && closed && makeIdentity(kind, std::strrchr(path, '/') + 1,
                                        "1.0.0", "driver.elf", false, &out);
}
}
using namespace RuntimePackages;
constexpr PackageRuntimePolicy kPolicy{"xtensa-esp32s3",2,8u*1024u*1024u,16u*1024u*1024u};
constexpr size_t kMaxProviders = 16;
struct Root { const char* path; Kind kind; };
constexpr Root kRoots[]={{"/Drivers",Kind::Driver},{"/Providers",Kind::Provider},{"/Services",Kind::Service}};
// Only fields reached by nextProvider; unused acquisition workspaces are omitted.
struct RegistrationFrame { char target[96]{}, name[160]{}, id[64]{}, capability[64]{}; Identity identity{}; };
bool prepare() { return true; } // Already-prepared graph; no activation.
