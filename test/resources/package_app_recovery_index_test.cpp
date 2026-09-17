#include "runtime/packages/PackageAppRecoveryIndex.h"
#include <cassert>
#include <cstdio>
#include <string>

int main() {
  std::string name;
  using RuntimePackages::appRecoveryCandidate;
  for (const char* artifact : {"sample.elf", "sample.elf.bak", "sample.json.bak",
                                "sample.elf.part", "sample.json.part"}) {
    assert(appRecoveryCandidate(artifact, name));
    assert(name == "sample.elf");
  }
  assert(appRecoveryCandidate("springboard.json.bak", name));
  assert(name == "springboard.elf");
  for (const char* unrelated : {"", "sample.json", "app-catalog.json", "readme.md",
                                "sample.elf.part.part", "../sample.elf", "a/b.elf",
                                ".elf", ".json.bak", "bad..name.elf.bak"}) {
    name = "stale";
    assert(!appRecoveryCandidate(unrelated, name));
    assert(name.empty());
  }
  assert(!appRecoveryCandidate(nullptr, name));
  assert(appRecoveryCandidate((std::string(123, 'a') + ".elf").c_str(), name));
  assert(name.size() == 127);
  assert(!appRecoveryCandidate((std::string(124, 'a') + ".elf").c_str(), name));
  std::puts("App recovery candidate indexing and filename bounds passed");
}
