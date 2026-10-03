#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_existing_bridge_fixture
#include "bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "runtime/drivers/ProviderModuleV2.h"
#include "runtime/packages/PackageUseGate.h"
using namespace RuntimePackages;
namespace {
void putResourceFile(const std::string& path, const std::string& data) {
  auto f = std::make_shared<TestFile>(); f->data.assign(data.begin(), data.end()); files[path] = f;
}
std::string resourceManifest(const char* kind) {
  const std::string sha(64, '0');
  return std::string("{\"schema\":2,\"kind\":\"") + kind + "\",\"id\":\"example\",\"version\":\"1.0.0\","
      "\"artifact\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":1,\"entries\":["
      "{\"name\":\"driver.elf\",\"size_bytes\":52,\"sha256\":\"" + sha + "\",\"executable\":true},"
      "{\"name\":\"assets/text.txt\",\"size_bytes\":3,\"sha256\":\"" + sha + "\",\"executable\":false}],\"requires\":[]}";
}
uint64_t revokedContext = 0;
void revokeOnRead(TestStorageOperation op) {
  if (op != TestStorageOperation::Read) return;
  testStorageHook = nullptr; nativeProviderStreamHost()->revoke(revokedContext);
}
void revokeOnOpen(TestStorageOperation op) {
  if (op != TestStorageOperation::Open) return;
  testStorageHook = nullptr; nativeProviderStreamHost()->close(revokedContext);
}
}
int main() {
  const auto* host = nativeProviderStreamHost(); assert(host && host->openResources);
  auto& gate = systemPackageUseGate();
  const char* kinds[] = {"driver", "service", "provider"};
  const char* roots[] = {"/Drivers/example", "/Services/example", "/Providers/example"};
  for (unsigned k = 0; k < 3; ++k) {
    Identity identity{}; assert(makeIdentity(static_cast<Kind>(k + 1), "example", "1.0.0", "driver.elf", false, &identity));
    risc_stream_provider_resources_v1 resources{};
    assert(!host->openResources(&resources, identity));
    assert(gate.pin(roots[k]));
    putResourceFile(std::string(roots[k]) + "/.package.json", resourceManifest(kinds[k]));
    putResourceFile(std::string(roots[k]) + "/assets/text.txt", "abc");
    assert(host->openResources(&resources, identity));
    const uint64_t context = resources.streams.context;
    assert(resources.streams.struct_size == sizeof(resources));
    uint32_t h = 99, n = 99;
    assert(resources.open_resource(context, "../other/file", &h) == T5_STREAM_INVALID && !h);
    assert(resources.open_resource(context, "driver.elf", &h) == T5_STREAM_DENIED && !h);
    uint32_t handles[4]{};
    for (auto& handle : handles) assert(resources.open_resource(context, "assets/text.txt", &handle) == T5_STREAM_OK);
    assert(resources.open_resource(context, "assets/text.txt", &h) == T5_STREAM_LIMIT && !h);
    assert(!host->grant(context, 1, 12345, handles[0], T5_STREAM_READ)); // resources are not public endpoints
    risc_stream_provider_v1 other{}; assert(host->open(&other));
    char out[4]{};
    assert(resources.read_resource(other.context, handles[0], out, 3, &n) == T5_STREAM_INVALID && !n);
    assert(resources.read_resource(context, handles[0], out, 3, &n) == T5_STREAM_OK && n == 3 && !std::strcmp(out, "abc"));
    assert(resources.seek_resource(context, handles[0], 0) == T5_STREAM_OK);
    assert(resources.streams.produce(context, handles[0], "x", 1, &n) == T5_STREAM_INVALID && !n);
    assert(gate.unpin(roots[k])); assert(!gate.beginReplacement(roots[k]));
    for (auto handle : handles) assert(resources.streams.close(context, handle) == T5_STREAM_OK);
    assert(gate.beginReplacement(roots[k])); assert(gate.endReplacement(roots[k]));
    assert(resources.open_resource(context, "assets/text.txt", &h) == T5_STREAM_OK);
    revokedContext = context; testStorageHook = revokeOnRead;
    std::memset(out, 'z', sizeof(out));
    assert(resources.read_resource(context, h, out, 3, &n) == T5_STREAM_CLOSED && !n);
    for (char c : out) assert(c == 'z');
    assert(!gate.pinned(roots[k]));
    assert(resources.open_resource(context, "assets/text.txt", &h) == T5_STREAM_DENIED && !h);
    host->close(context); host->close(other.context);
    assert(gate.pin(roots[k])); assert(host->openResources(&resources, identity));
    assert(gate.unpin(roots[k]));
    revokedContext = resources.streams.context; testStorageHook = revokeOnOpen;
    assert(resources.open_resource(revokedContext, "assets/text.txt", &h) == T5_STREAM_DENIED && !h);
    assert(!gate.pinned(roots[k]));
  }
  std::puts("Three installed provider kinds: resource scope, bounded handles, private rights, revoke during I/O and lock-safe retirement PASS");
}
