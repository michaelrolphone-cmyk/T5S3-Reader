#include "runtime/packages/PackageOnlineCatalog.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
using namespace RuntimePackages;

CatalogPackage package(Kind kind, const char* id, const char* version) {
  CatalogPackage p;
  assert(makeIdentity(kind, id, version, "driver.elf", false, &p.identity));
  std::strcpy(p.architecture, "xtensa-esp32s3");
  std::snprintf(p.archive, sizeof(p.archive), "driver-%s-%s-xtensa-esp32s3.rte.zip", id, version);
  p.sizeBytes = 1234;
  std::memset(p.sha256, 'a', 64);
  return p;
}
IndependentDriverRecord record(const char* id, const char* version, bool bundled = true) {
  IndependentDriverRecord row;
  std::strcpy(row.id, id); std::strcpy(row.version, version); row.bundled = bundled;
  std::snprintf(row.tag, sizeof(row.tag), "driver-%s-v%s", id, version);
  if (bundled) row.package = package(Kind::Driver, id, version);
  return row;
}
int main() {
  auto a = std::make_unique<PackageCatalog>();
  auto i = std::make_unique<IndependentDriverCatalog>();
  auto out = std::make_unique<OnlinePackageCatalog>();
  a->schema = i->schema = 1;
  std::strcpy(a->release, "aggregate-v1");
  const Kind kinds[] = {Kind::Application, Kind::Driver, Kind::Service, Kind::Provider};
  const char* ids[] = {"reader", "bus", "zip", "clock"};
  for (unsigned n = 0; n < 4; ++n) a->packages[n] = package(kinds[n], ids[n], "1.0.0");
  a->packageCount = 4;
  i->rows[0] = record("bus", "1.0.1");
  i->rows[1] = record("power", "2.0.0"); i->rowCount = 2;
  assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out));
  assert(out->packageCount == 5);
  assert(!std::strcmp(out->releases[0], "aggregate-v1"));
  assert(!std::strcmp(out->releases[3], "driver-bus-v1.0.1"));
  assert(!std::strcmp(out->releases[4], "driver-power-v2.0.0"));
  std::string url;
  assert(onlineArchiveUrl(out->packages[3], out->releases[3], "xtensa-esp32s3", url));
  assert(url.find("/download/driver-bus-v1.0.1/driver-bus-1.0.1-") != std::string::npos);
  const std::string pinned = url;
  std::strcpy(a->release, "changed-mutable-source");
  i->rows[0] = record("bus", "99.0.0");
  assert(onlineArchiveUrl(out->packages[3], out->releases[3], "xtensa-esp32s3", url) && url == pinned);

  // Numeric comparison, never lexical source priority.
  a->packages[1] = package(Kind::Driver, "bus", "1.10.0");
  i->rows[0] = record("bus", "1.9.9");
  assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out));
  assert(!std::strcmp(out->packages[1].identity.version, "1.10.0"));
  i->rows[0] = record("bus", "1.10.0");
  assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out));
  assert(!std::strcmp(out->releases[3], "driver-bus-v1.10.0"));
  for (unsigned fault = 0; fault < 5; ++fault) {
    i->rows[0] = record("bus", "1.10.0");
    auto& p = i->rows[0].package;
    if (fault == 0) ++p.sizeBytes;
    if (fault == 1) p.sha256[0] = 'b';
    if (fault == 2) std::strcpy(p.identity.artifact, "changed.elf");
    if (fault == 3) std::strcpy(p.architecture, "riscv32");
    if (fault == 4) std::strcpy(p.archive, "different.rte.zip");
    assert(!mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out));
    assert(out->packageCount == 0);
  }
  // Legacy versions are barriers. Newer valid ZIP may replace them; equal or
  // older aggregate is suppressed, never relabeled with the legacy ELF SHA.
  for (const auto version : {"1.9.9", "1.10.0", "1.10.1"}) {
    i->rows[0] = record("bus", version, false);
    assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out));
    assert(out->packageCount == (!std::strcmp(version, "1.9.9") ? 5u : 4u));
  }
  i->rows[0] = record("bus", "2.0.0");
  std::strcpy(i->rows[0].package.architecture, "riscv32");
  assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out) && out->packageCount == 4);
  assert(mergeOnlineCatalog(nullptr, *i, "riscv32", *out) && out->packageCount == 1);
  i->rowCount = 0;
  assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out) && out->packageCount == 4);
  assert(mergeOnlineCatalog(nullptr, *i, "xtensa-esp32s3", *out) && !out->packageCount);

  std::strcpy(i->appRows[0].id, "reader");
  i->appRowCount = 1;
  for (const auto version : {"0.9.9", "1.0.0", "1.0.1"}) {
    std::strcpy(i->appRows[0].version, version);
    assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out));
    assert(out->packageCount == (!std::strcmp(version, "0.9.9") ? 4u : 3u));
  }
  i->appRows[0] = record("reader", "1.0.1");
  i->appRows[0].package.identity.kind = Kind::Application;
  std::strcpy(i->appRows[0].tag, "app-reader-v1.0.1");
  assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out));
  assert(out->packageCount == 4 && !std::strcmp(out->releases[3], "app-reader-v1.0.1"));
  i->appRows[0] = record("reader", "1.0.0");
  i->appRows[0].package = a->packages[0];
  std::strcpy(i->appRows[0].tag, "app-reader-v1.0.0");
  assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out));
  assert(!std::strcmp(out->releases[3], "app-reader-v1.0.0"));
  ++i->appRows[0].package.sizeBytes;
  assert(!mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out) && !out->packageCount);
  i->appRowCount = 0;

  a->packageCount = kCatalogMaxPackages;
  for (size_t n = 0; n < kCatalogMaxPackages; ++n) {
    char id[32]; std::snprintf(id, sizeof(id), "app-%u", unsigned(n));
    a->packages[n] = package(Kind::Application, id, "1.0.0");
  }
  i->rows[0] = record("new", "1.0.0"); i->rowCount = 1;
  assert(!mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out) && !out->packageCount);

  // Full producer widths survive immutable selection, without truncation.
  const std::string longId(63, 'x');
  auto longRow = record(longId.c_str(), "4294967295.4294967295.429496729");
  assert(std::strlen(longRow.tag) > 64 && std::strlen(longRow.package.archive) > 95);
  assert(onlineArchiveUrl(longRow.package, longRow.tag, "xtensa-esp32s3", url));
  assert(url.find(longRow.tag) != std::string::npos);
  assert(std::string("/Packages/Inbox/").size() + std::strlen(longRow.package.archive) + 5 < kOnlineArchivePathBytes);
  longRow.package.sizeBytes = 4u * 1024u * 1024u + 65536u;
  assert(onlineArchiveUrl(longRow.package, longRow.tag, "xtensa-esp32s3", url));
  ++longRow.package.sizeBytes;
  assert(!onlineArchiveUrl(longRow.package, longRow.tag, "xtensa-esp32s3", url) && url.empty());
  assert(!onlineArchiveUrl(i->rows[0].package, "../unsafe", "xtensa-esp32s3", url));
  assert(!onlineArchiveUrl(i->rows[0].package, "valid?redirect=evil", "xtensa-esp32s3", url));
  // Module kinds share the same immutable selection rules without ID aliasing.
  clearIndependentDriverCatalog(*i); i->schema = 1;
  auto module = [](Kind kind, const char* prefix) {
    auto row = record("same-id", "1.2.3");
    row.package.identity.kind = kind;
    std::snprintf(row.tag, sizeof(row.tag), "%s-same-id-v1.2.3", prefix);
    std::snprintf(row.package.archive, sizeof(row.package.archive), "%s-same-id-1.2.3-xtensa-esp32s3.rte.zip", prefix);
    return row;
  };
  i->serviceRows[0] = module(Kind::Service, "service"); i->serviceRowCount = 1;
  i->providerRows[0] = module(Kind::Provider, "provider"); i->providerRowCount = 1;
  assert(mergeOnlineCatalog(nullptr, *i, "xtensa-esp32s3", *out) && out->packageCount == 2);
  assert(out->packages[0].identity.kind != out->packages[1].identity.kind);
  for (auto* row : {&i->serviceRows[0], &i->providerRows[0]}) {
    a->packageCount = 1; a->packages[0] = row->package;
    std::strcpy(a->packages[0].identity.version, "1.2.2");
    assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out) && out->packageCount == 2);
    a->packages[0] = row->package;
    ++a->packages[0].sizeBytes;
    assert(!mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out) && !out->packageCount);
    a->packages[0] = row->package;
    std::strcpy(a->packages[0].identity.version, "1.2.4");
    assert(mergeOnlineCatalog(a.get(), *i, "xtensa-esp32s3", *out) && out->packageCount == 2);
    assert(!std::strcmp(out->packages[0].identity.version, "1.2.4"));
  }
  i->providerRowCount = kIndependentCatalogMaxModulesPerKind + 1;
  assert(!mergeOnlineCatalog(nullptr, *i, "xtensa-esp32s3", *out) && !out->packageCount);
  std::puts("PASS: immutable per-package catalog merge, versions, legacy barriers, conflicts and limits");
}
