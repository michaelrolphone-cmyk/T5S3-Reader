#include "runtime/drivers/ProviderModuleV2.h"
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include <esp_elf.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <openssl/sha.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned relocations = 0;
static unsigned allocations = 0;
static unsigned fallback_allocations = 0;
static bool reject_allocations = false;
static bool reject_spiram = false;
static size_t expected_size = 9;
#define PACKAGE_ADMISSION_NO_MAIN
#include "../resources/package_executable_admission_test.cpp"
static const uint8_t* original = nullptr;
static const uint8_t* retainedExpected = nullptr;
static uint8_t expected_payload[] = {0x7f, 'E', 'L', 'F', 1, 1, 1, 0x44, 0x19};
static const char* const declared_imports[] = {"esp_intr_alloc", "malloc"};

extern "C" void* heap_caps_malloc(size_t size, uint32_t capabilities) {
  ++allocations;
  if ((capabilities & MALLOC_CAP_SPIRAM) == 0) ++fallback_allocations;
  if (reject_allocations || (reject_spiram && (capabilities & MALLOC_CAP_SPIRAM))) return nullptr;
  return std::malloc(size);
}
extern "C" void heap_caps_free(void* ptr) { std::free(ptr); }
extern "C" int esp_elf_relocate_privileged_verified_v1(esp_elf_t* image,
                                                         const uint8_t* bytes, size_t length,
                                                         const char* const* imports,
                                                         size_t import_count) {
  assert(image);
  ++relocations;
  /* Relocation still receives an independent snapshot and exact import
   * declarations. The actual helper validated cold mapped bytes first.
   * Deliberately fail before any native code can run. */
  assert(bytes != original);
  assert(length == expected_size);
  assert(std::memcmp(bytes, retainedExpected ? retainedExpected : original, length) == 0);
  assert(import_count == 2 && !std::strcmp(imports[0], declared_imports[0]) &&
         !std::strcmp(imports[1], declared_imports[1]));
  assert(retainedExpected ? imports != declared_imports : imports == declared_imports);
  return -1;
}
extern "C" void esp_elf_deinit(esp_elf_t*) {}

static bool attempt(const uint8_t* image, size_t length, const uint8_t digest[32]) {
  RuntimeProviders::ModuleV2 module;
  const bool loaded = module.loadVerifiedBytes(image, length, digest,
                                  declared_imports, 2, "fixture-snapshot",
                                  "cap.generic", 1, nullptr, 0);
  assert(!loaded && module.lastError()[0]);
  if (image && digest && length == sizeof(expected_payload) && !reject_allocations &&
      !std::memcmp(image,expected_payload,length)) {
    uint8_t actual[32]{};assert(SHA256(image,length,actual));
    if(!std::memcmp(digest,actual,32)) assert(std::strstr(module.lastError(), "elf-relocation-failed rc=-1"));
  }
  return loaded;
}

int main() {
  uint8_t candidate[sizeof(expected_payload)]{};
  std::memcpy(candidate, expected_payload, sizeof(candidate));
  original = candidate;
  uint8_t declared_digest[32]{};
  assert(SHA256(expected_payload, sizeof(expected_payload), declared_digest));

  assert(!attempt(candidate, sizeof(candidate), declared_digest));
  assert(relocations == 1 && allocations == 1 && fallback_allocations == 0);

  candidate[7] ^= 0x80;
  assert(!attempt(candidate, sizeof(candidate), declared_digest));
  assert(relocations == 1); /* Cold owned snapshot integrity rejects changed bytes. */
  candidate[7] ^= 0x80;
  uint8_t forged_digest[32]{};
  std::memcpy(forged_digest, declared_digest, sizeof(forged_digest));
  forged_digest[0] ^= 1;
  assert(!attempt(candidate, sizeof(candidate), forged_digest));
  assert(relocations == 1);

  reject_allocations = true;
  assert(!attempt(candidate, sizeof(candidate), declared_digest));
  assert(relocations == 1);
  reject_allocations = false;

  reject_spiram = true;
  assert(!attempt(candidate, sizeof(candidate), declared_digest));
  assert(relocations == 2 && fallback_allocations >= 1);
  reject_spiram = false;
  assert(!attempt(nullptr, sizeof(candidate), declared_digest));
  assert(!attempt(candidate, sizeof(candidate), nullptr));
  assert(!attempt(candidate, 8u * 1024u * 1024u + 1u, declared_digest));
  RuntimeProviders::ModuleV2 missing;
  assert(!missing.loadVerifiedBytes(candidate, sizeof(candidate), declared_digest,
                                    nullptr, 0, "fixture-snapshot", "cap.generic", 1,
                                    nullptr, 0));
  assert(relocations == 2);
  std::vector<uint8_t> managedBytes;
  assert(runPackageExecutableAdmissionTest(&managedBytes)==0);
  const std::string manifest=FakeSd::nodes.at("/services/clock/.package.json")->bytes;
  RuntimePackages::OrdinaryPackagePlan plan{};
  assert(RuntimePackages::parseOrdinaryManifest(manifest.data(),manifest.size(),plan));
  uint8_t manifestDigest[32]{},elfDigest[32]{};
  assert(RuntimePackages::packageSnapshotDigest(reinterpret_cast<const uint8_t*>(manifest.data()),manifest.size(),manifestDigest));
  assert(RuntimePackages::receiptDigest(plan.entries[0].sha256,elfDigest));
  assert(RuntimePackages::systemPackageUseGate().pin("/Services/clock"));
  expected_size=managedBytes.size();original=managedBytes.data();
  {RuntimeProviders::ModuleV2 module;
   assert(module.setResourceIdentity(plan.identity));
   assert(module.setPackageAdmission(manifestDigest,Storage.generation()));
   assert(!module.loadVerifiedBytes(managedBytes.data(),managedBytes.size(),elfDigest,declared_imports,2,"clock","cap.generic",1,nullptr,0));
   assert(std::strstr(module.lastError(),"elf-relocation-failed"));assert(relocations==3);}
  assert(Storage.writeFile("/changed-before-loader","x"));managedBytes.back()^=1;
  {RuntimeProviders::ModuleV2 module;
   assert(module.setResourceIdentity(plan.identity));
   assert(module.setPackageAdmission(manifestDigest,Storage.generation()));
   assert(!module.loadVerifiedBytes(managedBytes.data(),managedBytes.size(),elfDigest,declared_imports,2,"clock","cap.generic",1,nullptr,0));
   assert(std::strstr(module.lastError(),"installed-snapshot-integrity"));assert(relocations==3);}
  assert(RuntimePackages::systemPackageUseGate().unpin("/Services/clock"));
  // The actual graph binds private immutable ownership; public Module callers
  // above never get this optimization. Mutate the original caller after copy.
  managedBytes.back() ^= 1;
  const std::vector<uint8_t> expectedImage = managedBytes;
  retainedExpected = expectedImage.data(); original = managedBytes.data();
  assert(RuntimePackages::systemPackageUseGate().pin("/Services/clock"));
  {
    RuntimeProviders::GraphV2 graph;
    RuntimePackages::ManagerProviderCandidateV2 candidate{};
    candidate.driverId="clock";candidate.provides="cap.generic";candidate.providesApi=1;
    candidate.elfBytes=managedBytes.data();candidate.elfLength=managedBytes.size();
    candidate.importedSymbols=declared_imports;candidate.importedSymbolCount=2;
    candidate.declaredSha256=elfDigest;candidate.packageManifestSha256=manifestDigest;
    candidate.resourceIdentity=plan.identity;candidate.packageSourceStamp=Storage.generation();
    assert(RuntimePackages::DeviceProviderExecutorV2::registerManagerValidated(graph,candidate,false));
    managedBytes.back() ^= 1;
    assert(Storage.writeFile("/after-original-read","changed")); // old source epoch
    receiptTestHashBytes=0;
    assert(!graph.acquire("cap.generic",1).slot); // fixture relocation deliberately refuses
    assert(relocations==4&&receiptTestHashBytes>=expectedImage.size());
    assert(Storage.writeFile("/after-verification","changed again"));
    receiptTestHashBytes=0;
    assert(!graph.acquire("cap.generic",1).slot);
    assert(relocations==5&&receiptTestHashBytes<expectedImage.size());
    assert(Storage.writeFile("/Services/clock/.package.json","invalid current metadata"));
    assert(!graph.acquire("cap.generic",1).slot);assert(relocations==5);
    assert(Storage.writeFile("/Services/clock/.package.json",manifest));
    assert(graph.shutdown());
  }
  // A new graph cannot inherit the previous node's memory evidence.
  {
    RuntimeProviders::GraphV2 graph;
    RuntimePackages::ManagerProviderCandidateV2 candidate{};
    candidate.driverId="clock";candidate.provides="cap.generic";candidate.providesApi=1;
    candidate.elfBytes=managedBytes.data();candidate.elfLength=managedBytes.size();
    candidate.importedSymbols=declared_imports;candidate.importedSymbolCount=2;
    candidate.declaredSha256=elfDigest;candidate.packageManifestSha256=manifestDigest;
    candidate.resourceIdentity=plan.identity;candidate.packageSourceStamp=Storage.generation();
    assert(RuntimePackages::DeviceProviderExecutorV2::registerManagerValidated(graph,candidate,false));
    assert(Storage.writeFile("/new-lifetime","changed"));
    assert(!graph.acquire("cap.generic",1).slot);assert(relocations==5);
    assert(graph.shutdown());
  }
  assert(RuntimePackages::systemPackageUseGate().unpin("/Services/clock"));
  std::puts("Private graph image proof: first hash, remap after mutation without ELF rehash, caller-copy isolation and new-lifetime refusal PASS");
  std::puts("Privileged ELF: actual owned snapshot integrity/admission before relocation, allocation cleanup and mandatory imports PASS");
}
