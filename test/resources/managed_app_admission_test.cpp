#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <SdFat.h>

#include <cassert>
#include <cstdio>
#include <vector>

#include "native/ManagedAppAdmission.h"
#include "runtime/packages/PackageExecutableAdmission.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageUseGate.h"
#include "runtime/resources/ExecutionContext.h"
extern "C" bool esp_elf_admit_managed_app(const char*, const uint8_t*, size_t);
using namespace RuntimePackages;
std::string digestText(const uint8_t* bytes, size_t size) {
  uint8_t digest[32]{};
  assert(packageSnapshotDigest(bytes, size, digest));
  const char* hex = "0123456789abcdef";
  std::string out;
  for (auto b : digest) {
    out += hex[b >> 4];
    out += hex[b & 15];
  }
  return out;
}
int main() {
  assert(Storage.begin());
  assert(Storage.mkdir("/Apps"));
  assert(Storage.mkdir("/Apps/reader"));
  std::vector<uint8_t> elf(8192, 0x42);
  elf[0] = 0x7f;
  elf[1] = 'E';
  elf[2] = 'L';
  elf[3] = 'F';
  elf[4] = elf[5] = elf[6] = 1;
  elf[16] = 3;
  elf[17] = 0;
  elf[18] = 94;
  elf[19] = 0;
  elf[20] = 1;
  elf[21] = elf[22] = elf[23] = 0;
  const std::string sidecar =
      "{\"display_name\":\"Reader\",\"file_name\":\"reader.elf\",\"version\":\"1.0.0\","
      "\"min_firmware_version\":\"1.0.0\",\"icon\":\"solid:book\",\"requires\":[],\"optional\":[]}";
  const std::string manifest =
      "{\"schema\":1,\"kind\":\"application\",\"id\":\"reader\",\"version\":\"1.0.0\","
      "\"artifact\":\"reader.elf\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,\"entries\":["
      "{\"name\":\"reader.elf\",\"size_bytes\":8192,\"sha256\":\"" +
      digestText(elf.data(), elf.size()) +
      "\",\"executable\":true},"
      "{\"name\":\"reader.json\",\"size_bytes\":" +
      std::to_string(sidecar.size()) + ",\"sha256\":\"" +
      digestText(reinterpret_cast<const uint8_t*>(sidecar.data()), sidecar.size()) +
      "\",\"executable\":false}],\"requires\":[]}";
  assert(Storage.writeFile("/Apps/reader/.package.json", manifest));
  assert(Storage.writeFile("/Apps/reader/reader.json", sidecar));
  Identity identity{};
  assert(makeIdentity(Kind::Application, "reader", "1.0.0", "reader.elf", false, &identity));
  auto& gate = systemPackageUseGate();
  RuntimeResources::ExecutionContext context;
  const char* path = "/sd/Apps/reader/reader.elf";
  std::shared_ptr<const std::string> copied;
  assert(captureManagedAppSidecar(path, copied) == ManagedAppMetadata::Denied);
  assert(captureManagedAppSidecar("/sd/Apps/reader.elf", copied) == ManagedAppMetadata::Unmanaged);
  assert(esp_elf_admit_managed_app("/sd/Apps/reader.elf", elf.data(), elf.size()));
  assert(!esp_elf_admit_managed_app(path, elf.data(), elf.size()));
  assert(context.begin());
  assert(!beginManagedAppAdmission(identity, path));  // caller must pin
  assert(gate.pin("/Apps/reader"));
  Storage.externalStorageBegin();
  Storage.externalStorageEnd(true);
  const unsigned mounts = FakeSd::mounts;
  assert(beginManagedAppAdmission(identity, path));
  assert(FakeSd::mounts == mounts + 1 && Storage.generation().quiescent);
  assert(!beginManagedAppAdmission(identity, path));
  Identity captured{};
  assert(captureManagedAppSidecar(path, copied, &captured) == ManagedAppMetadata::Captured);
  assert(*copied == sidecar && !strcmp(captured.version, "1.0.0"));
  assert(esp_elf_admit_managed_app(path, elf.data(), elf.size()));
  assert(!esp_elf_admit_managed_app("/sd/Apps/other/reader.elf", elf.data(), elf.size()));
  assert(Storage.writeFile("/Apps/reader/reader.json", "changed after capture"));
  std::shared_ptr<const std::string> stable;
  assert(captureManagedAppSidecar(path, stable) == ManagedAppMetadata::Captured && *stable == sidecar);
  // Existing capability parsers receive the admitted immutable sidecar, never the changed file.
  assert(esp_elf_admit_managed_app(path, elf.data(), elf.size()));
  assert(Storage.writeFile("/outside", "observed mutation"));
  elf.back() ^= 1;
  assert(!esp_elf_admit_managed_app(path, elf.data(), elf.size()));
  elf.back() ^= 1;
  context.requestStop();
  assert(!esp_elf_admit_managed_app(path, elf.data(), elf.size()));
  std::shared_ptr<const std::string> denied;
  assert(captureManagedAppSidecar(path, denied) == ManagedAppMetadata::Denied);
  endManagedAppAdmission();
  assert(gate.unpin("/Apps/reader"));
  assert(gate.pinned("/Apps/reader"));  // outstanding firmware metadata view owns its pin
  stable.reset();
  copied.reset();
  assert(!gate.pinned("/Apps/reader"));
  context.end();
  assert(Storage.writeFile("/Apps/reader/reader.json", sidecar));
  RuntimeResources::ExecutionContext next;
  assert(next.begin());
  assert(gate.pin("/Apps/reader"));
  FakeSd::failClose = "/apps/reader/.package.json";
  assert(!beginManagedAppAdmission(identity, path));
  assert(gate.unpin("/Apps/reader"));
  assert(gate.pinned("/Apps/reader"));  // uncertain metadata close retains only its own extra pin
  next.end();
  puts(
      "Managed app admission: exact sidecar snapshot, actual ELF bytes, owner/path/pin scopes, revoke and "
      "uncertain-close retention PASS");
}
