#!/usr/bin/env python3
"""Exercise the actual canonical-app identity/sidecar guard, without SD I/O."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/native/NativeAppHost.cpp').read_text()
start = source.index('bool verifiedManagedApp(')
end = source.index('\n} // namespace', start)
function = source[start:end]
prefix = r'''
#include "runtime/packages/PackageIdentity.h"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
struct t5_app_manifest_t { char file_name[128]{}; };
static const int kCanonicalAppPolicy = 0;
static bool directoryValid = true, filesPresent = true, sidecarValid = true;
static std::string version = "1.2.3", filename = "clock.elf";
namespace RuntimePackages {
uint32_t installedCapabilityVersion(const char*) { return 1; }
bool inspectInstalledOrdinarySdDirectory(const char* path, int,
    uint32_t (*)(const char*), Identity& out) {
  assert(std::string(path) == "/Apps/clock");
  return directoryValid && makeIdentity(Kind::Application, "clock", "1.2.3", "clock.elf", false, &out);
}
}
bool t5_safe_elf_name(const char* value) { return RuntimePackages::safeArtifact(value); }
struct FakeStorage { bool exists(const char*) { return filesPresent; } } Storage;
bool readAppManifest(const char* path, t5_app_manifest_t& out, std::string* outVersion, bool required) {
  assert(std::string(path) == "/Apps/clock/clock.json" && outVersion && required);
  std::strcpy(out.file_name, filename.c_str());
  *outVersion = version;
  return sidecarValid && !version.empty();
}
'''
tests = r'''
int main() {
  RuntimePackages::Identity identity{}; t5_app_manifest_t manifest{};
  assert(verifiedManagedApp("clock", identity, &manifest));
  assert(std::string(identity.version) == "1.2.3" && std::string(manifest.file_name) == "clock.elf");
  for (const auto wrong : {"", "1.2.2", "1.2.4"}) {
    version = wrong; assert(!verifiedManagedApp("clock", identity));
  }
  version = "1.2.3"; filename = "other.elf";
  assert(!verifiedManagedApp("clock", identity));
  filename = "clock.elf"; directoryValid = false;
  assert(!verifiedManagedApp("clock", identity));
  directoryValid = true; filesPresent = false;
  assert(!verifiedManagedApp("clock", identity));
  filesPresent = true; sidecarValid = false;
  assert(!verifiedManagedApp("clock", identity));
  sidecarValid = true;
  assert(verifiedManagedApp("clock", identity));
  assert(!verifiedManagedApp("../clock", identity));
  std::puts("PASS: managed app requires matching ordinary identity and versioned sidecar, with recovery");
}
'''
with tempfile.TemporaryDirectory(prefix='managed-app-identity-') as temporary:
    path = Path(temporary)
    (path/'test.cpp').write_text(prefix+function+tests)
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-I'+str(ROOT/'src'), str(path/'test.cpp'), '-o', str(path/'test')]
    if os.environ.get('MV_SANITIZE') == '1':
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True)
    subprocess.run([str(path/'test')], check=True)
