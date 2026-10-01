// Real stream bridge, with storage asserting no I/O under its global mutex.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_existing_bridge_fixture
#include "bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include <T5PackageResourceApi.h>
#include "runtime/packages/PackageUseGate.h"
using namespace RuntimePackages;
namespace {
void put(const char* path, const std::string& data) {
  auto f = std::make_shared<TestFile>();
  f->data.assign(data.begin(), data.end()); files[path] = f;
}
std::string manifest(const char* version = "1.0.0") {
  const std::string sha(64, '0');
  return std::string("{\"schema\":2,\"kind\":\"application\",\"id\":\"example\",\"version\":\"") + version +
      "\",\"artifact\":\"example.elf\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":1,\"entries\":["
      "{\"name\":\"example.elf\",\"size_bytes\":52,\"sha256\":\"" + sha + "\",\"executable\":true},"
      "{\"name\":\"assets/text.txt\",\"size_bytes\":3,\"sha256\":\"" + sha + "\",\"executable\":false}],\"requires\":[]}";
}
void replaceContext(TestStorageOperation op) {
  if (op != TestStorageOperation::Open) return;
  testStorageHook = nullptr; nativeStreamsEnd(); nativeStreamsBegin();
}
}
namespace {
std::string importConsumer() {
 const std::string hash(64,'0');
 return "{\"schema\":3,\"payload\":\"executable\",\"kind\":\"application\",\"id\":\"consumer\",\"version\":\"1.0.0\","
 "\"artifact\":\"consumer.elf\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":1,"
 "\"entries\":[{\"name\":\"consumer.elf\",\"size_bytes\":52,\"sha256\":\""+hash+"\",\"executable\":true}],"
 "\"requires\":[],\"resource_imports\":[{\"id\":\"reference-pack\",\"min_version\":\"1.0.0\"}]}";
}
std::string dataPack(const char* version="1.0.0",bool executable=false) {
 const std::string hash(64,'0');
 std::string entries="{\"name\":\"help/data.txt\",\"size_bytes\":3,\"sha256\":\""+hash+"\",\"executable\":false}";
 if(executable)entries+=",{\"name\":\"driver.elf\",\"size_bytes\":52,\"sha256\":\""+hash+"\",\"executable\":true}";
 return std::string("{\"schema\":3,\"payload\":\"")+(executable?"executable":"resources")+
 "\",\"kind\":\"service\",\"id\":\"reference-pack\",\"version\":\""+version+"\",\"artifact\":"+
 (executable?"\"driver.elf\"":"null")+",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":1,"
 "\"entries\":["+entries+"],\"requires\":[],\"resource_imports\":[]}";
}
void mutateImportRead(TestStorageOperation op){
 if(op==TestStorageOperation::Read){testStorageHook=nullptr;testStorageGeneration.mutationAttempt();}
}
int failingImportClose = 0;
void failImportMetadataClose(TestStorageOperation op) {
 if(op==TestStorageOperation::Finish && --failingImportClose==0){
   closeOk=false;testStorageHook=nullptr;
 }
}
void checkImports(){
 Identity caller{};assert(makeIdentity(Kind::Application,"consumer","1.0.0","consumer.elf",false,&caller));
 auto& gate=systemPackageUseGate();
 put("/Apps/consumer/.package.json",importConsumer());
 put("/Services/reference-pack/.package.json",dataPack());put("/Services/reference-pack/help/data.txt","xyz");
 nativeStreamsBegin();api=t5_stream_get_api(1);assert(api&&gate.pin("/Apps/consumer"));
 assert(nativeStreamsBindPackageResources(caller));
 const auto* resource=t5_package_resource_get_api(1);assert(resource&&resource->open_import);
 t5_stream_t handle=99;uint32_t count=0;char bytes[4]{};
 assert(resource->open_import(1,"help/data.txt",&handle)==T5_STREAM_DENIED&&!handle);
 assert(resource->open_import(0,"../outside",&handle)==T5_STREAM_INVALID&&!handle);
 assert(resource->open_import(0,"help/unknown.txt",&handle)==T5_STREAM_DENIED&&!handle);
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_OK);
 assert(!gate.beginReplacement("/Services/reference-pack"));
 assert(api->read(handle,bytes,3,&count)==T5_STREAM_OK&&count==3&&!memcmp(bytes,"xyz",3));
 assert(api->close(handle)==T5_STREAM_OK&&!gate.pinned("/Services/reference-pack"));
 assert(gate.beginReplacement("/Services/reference-pack"));
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_BUSY&&!handle);
 assert(gate.endReplacement("/Services/reference-pack"));
 put("/Services/reference-pack/.package.json",dataPack("0.9.0"));
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_DENIED&&!handle);
 put("/Services/reference-pack/.package.json",dataPack("1.0.0",true));
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_DENIED&&!handle);
 put("/Services/reference-pack/.package.json",dataPack());
 testStorageGeneration.externalBegin();
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_BUSY&&!handle);
 testStorageGeneration.externalEnd(true);assert(testStorageGeneration.mountAttempt());testStorageGeneration.mounted(true);
 testStorageHook=mutateImportRead;
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_IO&&!handle&&!gate.pinned("/Services/reference-pack"));
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_OK);
 testStorageHook=mutateImportRead;
 assert(api->read(handle,bytes,3,&count)==T5_STREAM_IO&&!count);
 assert(api->close(handle)==T5_STREAM_OK);
 for(int failed=1;failed<=3;++failed){
  failingImportClose=failed;testStorageHook=failImportMetadataClose;
  assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_IO&&!handle);
  const char* retained=failed==1?"/Apps/consumer":"/Services/reference-pack";
  assert(gate.pinned(retained)&&!gate.beginReplacement(retained));
  closeOk=true;testStorageHook=nullptr;
  // Test isolation only: production intentionally retains uncertain ownership.
  assert(gate.unpin(retained));
 }
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_OK);
 nativeStreamsEnd();assert(!gate.pinned("/Services/reference-pack"));
 assert(resource->open_import(0,"help/data.txt",&handle)==T5_STREAM_DENIED&&!handle);
 assert(gate.unpin("/Apps/consumer"));
 std::puts("Declared resource imports: actual data-only bytes, private scope, version, pins, replacement, mutation and revoke PASS");
}
}

int main() {
  checkImports();
  Identity identity{}; assert(makeIdentity(Kind::Application, "example", "1.0.0", "example.elf", false, &identity));
  put("/Apps/example/.package.json", manifest()); put("/Apps/example/assets/text.txt", "abc");
  nativeStreamsBegin(); api = t5_stream_get_api(1); assert(api);
  assert(!t5_package_resource_get_api(1)); // legacy/unauthorized root
  assert(!nativeStreamsBindPackageResources(identity)); // loader must own its pin
  auto& gate = systemPackageUseGate(); assert(gate.pin("/Apps/example"));
  assert(nativeStreamsBindPackageResources(identity));
  assert(!nativeStreamsBindPackageResources(identity));
  const auto* resources = t5_package_resource_get_api(1); assert(resources);
  assert(!t5_package_resource_get_api(2));
  t5_stream_t h = 99;
  assert(resources->open("../other/data", &h) == T5_STREAM_INVALID && !h);
  assert(resources->open("example.elf", &h) == T5_STREAM_DENIED && !h);
  assert(resources->open("assets/unknown.txt", &h) == T5_STREAM_DENIED && !h);
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_OK && h);
  assert(gate.unpin("/Apps/example")); // stream itself retains exact generation
  assert(!gate.beginReplacement("/Apps/example"));
  char data[4]{}; uint32_t n = 99;
  assert(api->write(h, "z", 1, &n) == T5_STREAM_DENIED && !n);
  assert(api->read(h, data, sizeof(data), &n) == T5_STREAM_OK && n == 3 && !std::strcmp(data, "abc"));
  assert(api->seek(h, 0) == T5_STREAM_OK);
  assert(api->close(h) == T5_STREAM_OK);
  assert(gate.beginReplacement("/Apps/example")); assert(gate.endReplacement("/Apps/example"));
  put("/Apps/example/.package.json", manifest("2.0.0"));
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_IO && !h);
  put("/Apps/example/.package.json", manifest());
  put("/Apps/example/assets/text.txt", "changed length");
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_IO && !h);
  put("/Apps/example/assets/text.txt", "abc");
  testStorageHook = replaceContext;
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_DENIED && !h);
  assert(!gate.pinned("/Apps/example")); assert(!t5_package_resource_get_api(1));
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_DENIED && !h);
  assert(gate.pin("/Apps/example")); assert(nativeStreamsBindPackageResources(identity));
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_OK);
  assert(gate.unpin("/Apps/example")); nativeStreamsEnd();
  assert(!gate.pinned("/Apps/example"));
  nativeStreamsBegin(); assert(gate.pin("/Apps/example")); assert(nativeStreamsBindPackageResources(identity));
  assert(resources->open("assets/text.txt", &h) == T5_STREAM_OK);
  assert(gate.unpin("/Apps/example")); closeOk = false;
  assert(api->finish(h) == T5_STREAM_IO); assert(api->close(h) == T5_STREAM_OK);
  assert(gate.pinned("/Apps/example")); // uncertain close must not permit replacement
  closeOk = true; nativeStreamsEnd();
  std::puts("Installed resource stream scope, pins, identity, revocation and uncertain close PASS");
}
