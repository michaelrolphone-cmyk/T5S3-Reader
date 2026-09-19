#include "runtime/packages/PackageCatalog.h"
#include "runtime/packages/PackageRteZip.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

using namespace RuntimePackages;

namespace {
struct Blob {
  std::vector<uint8_t> bytes;
  bool readAt(uint64_t at, uint8_t* dest, size_t count) const {
    if (!dest || at > bytes.size() || count > bytes.size() - static_cast<size_t>(at))
      return false;
    if (count) std::memcpy(dest, bytes.data() + at, count);
    return true;
  }
};

std::string hex64(char fill) { return std::string(64, fill); }

std::string manifestJson(const char* kind, const char* name, uint32_t size,
                         const char* sha) {
  return std::string("{\"schema\":1,\"kind\":\"") + kind +
         "\",\"id\":\"sample-module\",\"version\":\"1.2.3\","
         "\"artifact\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\","
         "\"min_runtime_api\":2,\"entries\":[{\"name\":\"" + name +
         "\",\"size_bytes\":" + std::to_string(size) + ",\"sha256\":\"" + sha +
         "\",\"executable\":true}],\"requires\":[]}";
}

Blob packFiles(const std::vector<RteZipSourceFile>& files) {
  Blob out;
  auto write = [&](const uint8_t* data, size_t length) {
    out.bytes.insert(out.bytes.end(), data, data + length);
    return true;
  };
  uint32_t written = 0;
  assert(packRteZip(files.data(), files.size(), write, &written) ==
         RteZipResult::Ready);
  assert(written == out.bytes.size());
  return out;
}
} // namespace

int main() {
  const char* kinds[] = {"application", "driver", "service", "provider"};
  uint8_t elf[52]{};
  elf[0] = 0x7f;
  elf[1] = 'E';
  elf[2] = 'L';
  elf[3] = 'F';
  const std::string sha = hex64('a');
  for (unsigned k = 0; k < 4; ++k) {
    std::string json = manifestJson(kinds[k], "driver.elf",
                                   static_cast<uint32_t>(sizeof(elf)), sha.c_str());
    RteZipSourceFile files[] = {
        {kOrdinaryManifestName,
         reinterpret_cast<const uint8_t*>(json.data()),
         static_cast<uint32_t>(json.size())},
        {"driver.elf", elf, static_cast<uint32_t>(sizeof(elf))},
    };
    Blob zip = packFiles({files[0], files[1]});
    auto reader = [&](uint64_t at, uint8_t* dest, size_t count) {
      return zip.readAt(at, dest, count);
    };
    RteZipView view{};
    assert(inspectRteZip(reader, zip.bytes.size(), view) == RteZipResult::Ready);
    assert(view.entryCount == 2 && view.manifestIndex == 0);
    uint8_t scratch[2048]{};
    OrdinaryPackagePlan plan{};
    assert(planRteZip(reader, view, scratch, sizeof(scratch), plan) ==
           RteZipResult::Ready);
    assert(static_cast<unsigned>(plan.identity.kind) == k);
    assert(std::string(plan.identity.id) == "sample-module");
    assert(plan.entryCount == 1);
    uint8_t recovered[52]{};
    assert(readRteZipEntry(reader, view, 1, recovered, sizeof(recovered)) ==
           RteZipResult::Ready);
    assert(std::memcmp(recovered, elf, sizeof(elf)) == 0);
  }

  {
    const uint8_t payload[] = {'x'};
    RteZipSourceFile bad[] = {
        {kOrdinaryManifestName, payload, 1},
        {"../escape.elf", payload, 1},
    };
    Blob out;
    auto write = [&](const uint8_t* data, size_t length) {
      out.bytes.insert(out.bytes.end(), data, data + length);
      return true;
    };
    assert(packRteZip(bad, 2, write, nullptr) == RteZipResult::UnsafePath);
  }
  {
    const uint8_t payload[] = {'x'};
    RteZipSourceFile dup[] = {
        {kOrdinaryManifestName, payload, 1},
        {"driver.elf", payload, 1},
        {"DRIVER.ELF", payload, 1},
    };
    Blob out;
    auto write = [&](const uint8_t* data, size_t length) {
      out.bytes.insert(out.bytes.end(), data, data + length);
      return true;
    };
    assert(packRteZip(dup, 3, write, nullptr) == RteZipResult::DuplicateName);
  }

  const std::string catalog =
      "{\"schema\":1,\"release\":\"u1-host\","
      "\"packages\":["
      "{\"kind\":\"application\",\"id\":\"desk-clock\",\"version\":\"1.0.0\","
      "\"artifact\":\"app.elf\",\"architecture\":\"xtensa-esp32s3\","
      "\"archive\":\"application-desk-clock-1.0.0-xtensa-esp32s3.rte.zip\","
      "\"size_bytes\":128,\"sha256\":\"" + hex64('1') + "\"},"
      "{\"kind\":\"driver\",\"id\":\"usb-cdc-acm\",\"version\":\"0.2.0\","
      "\"artifact\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\","
      "\"archive\":\"driver-usb-cdc-acm-0.2.0-xtensa-esp32s3.rte.zip\","
      "\"size_bytes\":256,\"sha256\":\"" + hex64('2') + "\"},"
      "{\"kind\":\"service\",\"id\":\"archive-zip\",\"version\":\"1.0.0\","
      "\"artifact\":\"service.elf\",\"architecture\":\"xtensa-esp32s3\","
      "\"archive\":\"service-archive-zip-1.0.0-xtensa-esp32s3.rte.zip\","
      "\"size_bytes\":64,\"sha256\":\"" + hex64('3') + "\"},"
      "{\"kind\":\"provider\",\"id\":\"i2c-esp32s3\",\"version\":\"0.2.0\","
      "\"artifact\":\"provider.elf\",\"architecture\":\"xtensa-esp32s3\","
      "\"archive\":\"provider-i2c-esp32s3-0.2.0-xtensa-esp32s3.rte.zip\","
      "\"size_bytes\":96,\"sha256\":\"" + hex64('4') + "\"}]}";
  PackageCatalog parsed{};
  assert(parsePackageCatalog(catalog.data(), catalog.size(), parsed));
  assert(parsed.schema == 1 && parsed.packageCount == 4);
  assert(std::string(parsed.packages[1].identity.id) == "usb-cdc-acm");
  assert(std::strstr(parsed.packages[1].archive, ".rte.zip"));

  PackageCatalog rejected{};
  const std::string usbCatalog =
      "{\"schema\":1,\"release\":\"legacy\","
      "\"packages\":[{\"kind\":\"driver\",\"id\":\"usb-cdc-acm\","
      "\"version\":\"0.1.0\",\"artifact\":\"driver.elf\","
      "\"architecture\":\"xtensa-esp32s3\","
      "\"archive\":\"usb-provider-catalog.json\","
      "\"size_bytes\":12,\"sha256\":\"" + hex64('5') + "\"}]}";
  assert(!parsePackageCatalog(usbCatalog.data(), usbCatalog.size(), rejected));
  assert(rejected.packageCount == 0);
  return 0;
}
