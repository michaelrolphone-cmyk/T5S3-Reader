#include "runtime/packages/PackageOrdinaryManifest.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace RuntimePackages;

static std::string manifest(const char* kind = "driver") {
  return std::string("{\"schema\":1,\"kind\":\"") + kind +
      "\",\"id\":\"sample-module\",\"version\":\"1.2.3\","
      "\"artifact\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\","
      "\"min_runtime_api\":2,\"entries\":[{\"name\":\"driver.elf\","
      "\"size_bytes\":96,\"sha256\":\"" + std::string(64, 'a') +
      "\",\"executable\":true},{\"name\":\"readme.txt\","
      "\"size_bytes\":16,\"sha256\":\"" + std::string(64, 'b') +
      "\",\"executable\":false}],\"requires\":[{\"capability\":"
      "\"usb.host\",\"min_api\":1}]}";
}
static bool accepted(const std::string& bytes, OrdinaryPackagePlan& plan) {
  return parseOrdinaryManifest(bytes.data(), bytes.size(), plan);
}
static void reject(const std::string& bytes) {
  OrdinaryPackagePlan plan{};
  assert(!accepted(bytes, plan));
  assert(plan.entryCount == 0 && plan.requirementCount == 0);
}
static std::string replace(std::string source, const std::string& old,
                           const std::string& replacement) {
  const size_t pos = source.find(old);
  assert(pos != std::string::npos);
  source.replace(pos, old.size(), replacement);
  return source;
}
int main() {
  const char* kinds[] = {"application", "driver", "service", "provider"};
  for (unsigned i = 0; i < 4; ++i) {
    OrdinaryPackagePlan plan{};
    const std::string bytes = manifest(kinds[i]);
    assert(accepted(bytes, plan));
    assert(static_cast<unsigned>(plan.identity.kind) == i);
    assert(std::string(plan.identity.id) == "sample-module");
    assert(std::string(plan.identity.version) == "1.2.3");
    assert(std::string(plan.identity.artifact) == "driver.elf");
    assert(plan.entryCount == 2 && plan.requirementCount == 1);
    assert(plan.requirements[0].minApi == 1);
    assert(plan.entries[0].sizeBytes == 96 && plan.entries[0].executable);
    assert(plan.entries[1].sizeBytes == 16 && !plan.entries[1].executable);
    // Parsing declarations never grants dependencies: real preflight refuses.
    const PackageRuntimePolicy runtime{"xtensa-esp32s3", 2, 0, 1024, 4096};
    assert(preflightOrdinaryPackage(plan, runtime,
               [](const char*) -> uint32_t { return 0; }) ==
           PreflightResult::UnavailableCapability);
  }
  OrdinaryPackagePlan plan{};
  std::string bytes = manifest();
  reject("");
  reject(bytes + "garbage");
  reject(replace(bytes, "\"schema\":1", "\"schema\":2"));
  reject(replace(bytes, "\"kind\":\"driver\"", "\"kind\":\"unknown\""));
  reject(replace(bytes, "\"id\":\"sample-module\"", "\"id\":\"../escape\""));
  reject(replace(bytes, "\"version\":\"1.2.3\"", "\"version\":\"01.2.3\""));
  reject(replace(bytes, "\"architecture\":\"xtensa-esp32s3\"",
                 "\"architecture\":\"unknown\""));
  reject(replace(bytes, "\"min_runtime_api\":2", "\"min_runtime_api\":0"));
  reject(replace(bytes, "\"min_runtime_api\":2", "\"min_runtime_api\":4294967296"));
  reject(replace(bytes, "\"size_bytes\":96", "\"size_bytes\":0"));
  reject(replace(bytes, "\"size_bytes\":96", "\"size_bytes\":18446744073709551616"));
  reject(replace(bytes, "\"size_bytes\":96", "\"size_bytes\":95.0"));
  reject(replace(bytes, "\"size_bytes\":96", "\"size_bytes\":1048577"));
  reject(replace(bytes, "\"sha256\":\"" + std::string(64, 'a') + "\"",
                 "\"sha256\":\"deadbeef\""));
  reject(replace(bytes, "\"executable\":true", "\"executable\":\"true\""));
  reject(replace(bytes, "\"executable\":true", "\"executable\":false"));
  reject(replace(bytes, "\"name\":\"readme.txt\"", "\"name\":\"driver.elf\""));
  reject(replace(bytes, "\"name\":\"readme.txt\"", "\"name\":\"other.elf\""));
  reject(replace(bytes, "\"min_api\":1", "\"min_api\":0"));
  reject(replace(bytes, "\"requires\":[{\"capability\":\"usb.host\",\"min_api\":1}]",
                 "\"requires\":[{\"capability\":\"usb.host\",\"min_api\":1},"
                 "{\"capability\":\"usb.host\",\"min_api\":1}]"));
  reject(replace(bytes, "\"requires\":[{\"capability\":\"usb.host\",\"min_api\":1}]",
                 "\"requires\":{}"));
  reject(replace(bytes, "\"schema\":1", "\"schema\":1,\"schema\":1"));
  reject(replace(bytes, "\"schema\":1", "\"schema\":1,\"unknown\":1"));
  reject(replace(bytes, "\"id\":\"sample-module\"", "\"id\":\"sample\\u002dmodule\""));
  reject(replace(bytes, "\"artifact\":\"driver.elf\"", "\"artifact\":\"../driver.elf\""));
  reject(replace(bytes, "\"artifact\":\"driver.elf\"", "\"artifact\":\"readme.txt\""));
  reject(replace(bytes, "\"min_runtime_api\":2,", ""));
  reject(replace(bytes, "\"requires\":[{\"capability\":\"usb.host\",\"min_api\":1}]",
                 "\"requires\":[] ,\"requires\":[]"));
  reject(bytes.substr(0, bytes.size() - 1));
  bytes = replace(bytes, "\"requires\":[{\"capability\":\"usb.host\",\"min_api\":1}]",
                  "\"requires\":[]");
  assert(accepted(bytes, plan) && plan.requirementCount == 0);
  std::puts("Canonical manifest: four kinds, strict bounds/types/identity, aliases, duplicate keys and dependency separation PASS");
}
