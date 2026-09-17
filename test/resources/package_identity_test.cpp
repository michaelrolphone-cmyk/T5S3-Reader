#include "runtime/packages/PackageIdentity.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace RuntimePackages;

int main() {
  Identity app{};
  assert(makeIdentity(Kind::Application, nullptr, "1.0.1", "app_store.elf", false, &app));
  assert(app.kind == Kind::Application && std::strcmp(app.id, "app_store") == 0);
  assert(std::strcmp(app.version, "1.0.1") == 0 && !app.legacyVersion);

  Identity explicitApp{};
  assert(makeIdentity(Kind::Application, "org_example_terminal", "2.3.4",
                      "app_store.elf", false, &explicitApp));
  assert(!samePackage(app, explicitApp));
  Identity legacy{};
  assert(makeIdentity(Kind::Application, nullptr, nullptr, "app_store.elf", true, &legacy));
  assert(legacy.legacyVersion && legacy.version[0] == '\0');
  assert(samePackage(app, legacy));
  assert(!makeIdentity(Kind::Application, nullptr, nullptr, "app_store.elf", false, &legacy));

  Identity driver{};
  assert(makeIdentity(Kind::Driver, "gps-nmea", "1.0.0", "driver.elf", false, &driver));
  assert(driver.kind == Kind::Driver && std::strcmp(driver.id, "gps-nmea") == 0);
  assert(!samePackage(app, driver));
  Identity service{};
  assert(makeIdentity(Kind::Service, "gps-nmea", "1.0.0", "driver.elf", false, &service));
  assert(!samePackage(driver, service));
  assert(!makeIdentity(Kind::Driver, nullptr, "1.0.0", "driver.elf", false, &driver));
  assert(!makeIdentity(Kind::Driver, "", "1.0.0", "driver.elf", false, &driver));
  assert(!makeIdentity(Kind::Application, "", "1.0.0", "app.elf", false, &app));
  assert(!makeIdentity(Kind::Driver, "gps-nmea", nullptr, "driver.elf", false, &driver));

  const char* badIds[] = {"-start", "_start", "Upper", "a.b", "a/b", "../escape", "a\\b", "a:b", "a b", "a\n"};
  for (const auto* bad : badIds)
    assert(!makeIdentity(Kind::Driver, bad, "1.0.0", "driver.elf", false, &driver));
  const char* badFiles[] = {"", ".hidden.elf", "../escape.elf", "a/escape.elf", "a\\escape.elf",
                            "a:escape.elf", "app.ELF", "app.elf.bak", "app..elf", "app.elf/"};
  for (const auto* bad : badFiles)
    assert(!makeIdentity(Kind::Application, nullptr, "1.0.0", bad, false, &app));
  const char* badVersions[] = {"", "1", "1.2", "1.2.", "1..2", "1.2.3.4", "v1.2.3",
                               "1.2.-3", "1.2.3-rc", "1.2.3+meta", "1.2.3/evil"};
  for (const auto* bad : badVersions)
    assert(!makeIdentity(Kind::Driver, "gps-nmea", bad, "driver.elf", false, &driver));

  char longId[70];
  std::memset(longId, 'a', sizeof(longId));
  longId[sizeof(longId) - 1] = '\0';
  assert(!safeId(longId));
  char longFile[145];
  std::memset(longFile, 'a', sizeof(longFile));
  std::memcpy(longFile + 139, ".elf", 5);
  assert(!safeArtifact(longFile));
  char longVersion[40];
  std::memset(longVersion, '1', sizeof(longVersion));
  longVersion[sizeof(longVersion) - 1] = '\0';
  assert(!safeVersion(longVersion));
  assert(!makeIdentity(Kind::Driver, "gps-nmea", "1.0.0", "driver.elf", false, nullptr));
  std::puts("Shared package identity: app, driver, service, legacy and hostile fields passed");
}
