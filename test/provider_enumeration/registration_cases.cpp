// The production registration directory walk uses real HalStorage and fault
// media. Graph/ELF admission is the seam: these tests never execute a provider.
namespace RuntimeProviders { struct GraphV2 {}; }
namespace RuntimePackages {
struct InstalledCapabilitySnapshot {};
uint32_t versionInInstalledSnapshot(const InstalledCapabilitySnapshot*, const char* capability) {
  return std::strcmp(capability, "absent") ? 1 : 0;
}
}
struct ProviderAncestry { RegistrationFrame frames[kMaxProviders]; };
char loadError[160]{};
unsigned registrations = 0;
bool failAfterRegistration = false;
bool failDependency = false;
bool providerFail(const char* stage, const char* identity) {
  std::snprintf(loadError, sizeof(loadError), "%s: %s", stage, identity);
  return false;
}
bool registerOne(RuntimeProviders::GraphV2&, const char*, const char*, Kind,
                 const char*, uint32_t, const InstalledCapabilitySnapshot*, ProviderAncestry&, size_t) {
  ++registrations;
  if (failDependency) return providerFail("Provider dependency ambiguous", "test.bus");
  if (failAfterRegistration) FakeSd::failDirectory = "/drivers";
  return true;
}

// REGISTRATION_FUNCTION

void package(const char* id, const char* capability) {
  const std::string root = std::string("/Drivers/") + id;
  assert(Storage.mkdir(root.c_str()));
  assert(Storage.writeFile((root + "/provider-abi.v1").c_str(),
      std::string("os-cpu-abi=1\nprovides=") + capability + "\napi=1\n"));
}
int main() {
  assert(Storage.begin() && Storage.mkdir("/Drivers"));
  package("a-target", "test.target");
  package("b-other", "test.other");
  RuntimeProviders::GraphV2 graph;
  InstalledCapabilitySnapshot snapshot;
  ProviderAncestry ancestry{};
  uint32_t selected = 0;
  assert(registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(selected == 1 && registrations == 1 && !loadError[0]);
  failAfterRegistration = true;
  assert(!registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "Provider directory read failed"));
  assert(registrations == 2); // Accepted prefix is never mistaken for complete EOF.
  failAfterRegistration = false;
  FakeSd::failDirectory.clear();
  assert(registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(selected == 1 && !loadError[0]);
  assert(!registerCapability(graph, &snapshot, "absent", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "Provider dependency unavailable: absent"));
  failDependency = true;
  assert(!registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "Provider dependency ambiguous: test.bus"));
  failDependency = false;
  assert(!registerCapability(graph, &snapshot, "no-match", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "Provider dependency unavailable: no-match"));
  package("c-target", "test.target");
  assert(!registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "Provider dependency ambiguous"));
  assert(Storage.writeFile("/Drivers/c-target/provider-abi.v1",
      "os-cpu-abi=1\nprovides=test.other\napi=1\n"));
  // Copy metadata may sort before the target. Do not truncate selection or
  // miss an ambiguous provider beyond the old 64-raw-entry boundary.
  for (unsigned n=0;n<64;++n)
    assert(Storage.writeFile(("/Drivers/._copy-"+std::to_string(n)).c_str(), "inert"));
  assert(Storage.writeFile("/Drivers/.DS_Store", "inert"));
  FakeSd::entryNames["/drivers/.ds_store"]=".DS_Store";
  assert(registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(selected==1);
  package("z-late-target", "test.target");
  assert(!registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "Provider dependency ambiguous"));
  assert(Storage.remove("/Drivers/z-late-target/provider-abi.v1"));
  assert(Storage.rmdir("/Drivers/z-late-target"));
  assert(Storage.writeFile("/Drivers/._copy-overflow", "inert"));
  assert(!registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "entry limit exceeded"));
  assert(Storage.remove("/Drivers/._copy-overflow"));
  for (unsigned n=3;n<64;++n)
    assert(Storage.writeFile(("/Drivers/unused-"+std::to_string(n)).c_str(), "ordinary"));
  assert(registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(Storage.writeFile("/Drivers/zz-overflow", "ordinary"));
  assert(!registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "entry limit exceeded"));
  assert(Storage.remove("/Drivers/zz-overflow"));
  for (unsigned n=3;n<64;++n)
    assert(Storage.remove(("/Drivers/unused-"+std::to_string(n)).c_str()));
  for (unsigned n=0;n<64;++n)
    assert(Storage.remove(("/Drivers/._copy-"+std::to_string(n)).c_str()));
  assert(Storage.remove("/Drivers/.DS_Store"));
  FakeSd::failClose = "/drivers";
  assert(!registerCapability(graph, &snapshot, "test.target", 1, &selected, ancestry, 0));
  assert(!selected && std::strstr(loadError, "Provider directory close failed"));
  FakeSd::failClose.clear();
  assert(!Storage.generation().quiescent); // Discarded uncertain close is retained as uncertainty.
  std::puts("Production registration: unique selection, ambiguity, read-after-prefix fault, checked close and current errors PASS");
}
