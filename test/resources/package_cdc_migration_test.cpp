#include <cassert>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/packages/PackageCdcMigration.h"
#include "runtime/packages/PackageOrdinaryStageRecovery.h"
using namespace RuntimePackages;
struct Node {
  Identity identity{};
  bool unknown = false, partial = false;
  uint8_t digest = 1;
};
struct Ops {
  std::map<std::string, Node> nodes;
  CdcMigrationIntent intent{};
  bool marker = false, badIntent = false, failClear = false, failPurge = false, legacy = false;
  int crash = 0, renames = 0;
  bool exists(const char* p) { return !std::strcmp(p, kCdcIntentPath) ? marker : nodes.count(p); }
  bool rename(const char* from, const char* to) {
    assert(!systemPackageUseGate().pin(kCdcCanonicalRoot));
    assert(!systemPackageUseGate().pin(kCdcAliasRoot));
    if (!nodes.count(from) || nodes.count(to)) return false;
    nodes[to] = nodes[from];
    nodes.erase(from);
    if (++renames == crash) throw 1;
    return true;
  }
  bool manifestDigest(const char* p, uint8_t (&out)[32]) {
    if (!nodes.count(p)) return false;
    std::memset(out, nodes[p].digest, 32);
    return true;
  }
  bool writeIntent(const CdcMigrationIntent& i) {
    if (marker) return false;
    intent = i;
    marker = true;
    return true;
  }
  bool readIntent(CdcMigrationIntent& i) {
    i = intent;
    return marker && !badIntent;
  }
  bool clearIntent(const CdcMigrationIntent&) {
    if (failClear) return false;
    marker = false;
    return true;
  }
  bool legacyPending() { return legacy; }
  bool verify(const char* p, Identity& i) {
    auto it = nodes.find(p);
    if (it == nodes.end() || it->second.unknown || it->second.partial) return false;
    i = it->second.identity;
    return true;
  }
  bool purgeHolding(const CdcMigrationIntent& i) {
    auto it = nodes.find(kCdcHoldingRoot);
    if (it == nodes.end() || it->second.digest != i.aliasManifestDigest[0] ||
        std::strcmp(it->second.identity.version, i.aliasVersion))
      return false;
    return purge(kCdcHoldingRoot);
  }
  bool purge(const char* p) {
    auto it = nodes.find(p);
    if (it == nodes.end() || it->second.unknown) return false;
    if (failPurge) {
      it->second.partial = true;
      return false;
    }
    nodes.erase(it);
    return true;
  }
};
Identity id(const char* name, const char* version) {
  Identity i{};
  assert(makeIdentity(Kind::Driver, name, version, "driver.elf", false, &i));
  return i;
}
OrdinaryTransactionPaths paths() {
  OrdinaryTransactionPaths p{};
  assert(ordinaryTransactionPaths(Kind::Driver, kCdcCanonicalId, p));
  return p;
}
Ops fixture(bool old = true) {
  Ops o;
  auto p = paths();
  o.nodes[kCdcAliasRoot] = {id(kCdcAliasId, "0.1.7")};
  o.nodes[p.stage] = {id(kCdcCanonicalId, "0.1.8"), false, false, 8};
  if (old) o.nodes[p.target] = {id(kCdcCanonicalId, "0.1.0")};
  return o;
}
OrdinaryTransactionResult publish(Ops& o) {
  Identity observed{};
  return publishCdcPackage(
      o, id(kCdcCanonicalId, "0.1.8"), [&](auto p, auto& i) { return o.verify(p, i); },
      [&](auto p) { return o.purge(p); }, observed);
}
OrdinaryTransactionResult recover(Ops& o) {
  Identity observed{};
  return recoverCdcMigration(
      o, [&](auto p, auto& i) { return o.verify(p, i); }, [&](auto p) { return o.purge(p); }, observed);
}
int main() {
  for (bool old : {false, true}) {
    auto o = fixture(old);
    assert(publish(o) == OrdinaryTransactionResult::Published);
    assert(!o.marker && !o.exists(kCdcAliasRoot) && !o.exists(kCdcHoldingRoot));
    assert(!std::strcmp(o.nodes[kCdcCanonicalRoot].identity.version, "0.1.8"));
  }
  for (int cut = 1; cut <= 3; ++cut) {
    auto o = fixture();
    o.crash = cut;
    try {
      (void)publish(o);
      assert(false);
    } catch (int) {
    }
    o.crash = 0;
    assert(o.marker);
    auto p = paths();
    assert(discardOrdinaryStage(
               o, Kind::Driver, kCdcCanonicalId, [](auto) { return true; }, [](auto) { return true; }) ==
           OrdinaryStageDiscardResult::RecoveryRequired);
    auto r = recover(o);
    assert(r == OrdinaryTransactionResult::InstalledVerified || r == OrdinaryTransactionResult::PreviousRestored);
    assert(!o.marker && !o.exists(kCdcHoldingRoot));
    if (cut < 3) {
      assert(o.exists(kCdcAliasRoot) && o.exists(p.stage));
      assert(!std::strcmp(o.nodes[p.target].identity.version, "0.1.0"));
      assert(publish(o) == OrdinaryTransactionResult::Published);
    } else
      assert(!o.exists(kCdcAliasRoot));
  }
  {
    auto o = fixture();
    o.nodes[kCdcAliasRoot].unknown = true;
    assert(publish(o) == OrdinaryTransactionResult::InvalidInstalled);
    assert(!o.marker && o.renames == 0);
  }
  for (const char* root : {kCdcAliasRoot, kCdcCanonicalRoot}) {
    auto o = fixture();
    assert(systemPackageUseGate().pin(root));
    assert(publish(o) == OrdinaryTransactionResult::InUse);
    assert(systemPackageUseGate().unpin(root));
    assert(!o.marker && o.renames == 0);
  }
  {
    auto o = fixture();
    o.nodes[kCdcAliasRoot].identity = id(kCdcAliasId, "0.1.8");
    assert(publish(o) == OrdinaryTransactionResult::VersionRejected);
    assert(!o.marker);
  }
  {
    auto o = fixture();
    o.nodes[kCdcCanonicalRoot].identity = id(kCdcCanonicalId, "0.1.9");
    assert(publish(o) == OrdinaryTransactionResult::VersionRejected);
    assert(!o.marker);
  }
  {
    auto o = fixture(false);
    o.failPurge = true;
    assert(publish(o) == OrdinaryTransactionResult::CleanupPending);
    assert(o.marker && o.exists(kCdcHoldingRoot));
    o.failPurge = false;
    assert(recover(o) == OrdinaryTransactionResult::InstalledVerified);
    assert(!o.marker);
  }
  {
    auto o = fixture();
    o.failClear = true;
    assert(publish(o) == OrdinaryTransactionResult::CleanupPending);
    o.failClear = false;
    assert(recover(o) == OrdinaryTransactionResult::InstalledVerified);
  }
  {
    auto o = fixture();
    o.crash = 3;
    try {
      (void)publish(o);
    } catch (int) {
    }
    o.crash = 0;
    o.nodes[kCdcCanonicalRoot].digest = 77;
    assert(recover(o) == OrdinaryTransactionResult::AmbiguousState);
    assert(o.marker && o.exists(kCdcHoldingRoot));
  }
  {
    auto o = fixture();
    o.crash = 1;
    try {
      (void)publish(o);
    } catch (int) {
    }
    o.crash = 0;
    o.badIntent = true;
    assert(recover(o) == OrdinaryTransactionResult::AmbiguousState);
    assert(o.marker && o.exists(kCdcHoldingRoot));
  }
  {
    auto o = fixture();
    o.legacy = true;
    assert(publish(o) == OrdinaryTransactionResult::AmbiguousState);
    assert(!o.marker);
  }
  {
    auto o = fixture(false);
    o.failPurge = true;
    assert(publish(o) == OrdinaryTransactionResult::CleanupPending);
    o.failPurge = false;
    o.nodes[kCdcHoldingRoot].digest = 99;
    assert(recover(o) == OrdinaryTransactionResult::CleanupPending);
    assert(o.marker && o.exists(kCdcHoldingRoot));
  }
  {
    auto o = fixture();
    o.crash = 1;
    try {
      (void)publish(o);
    } catch (int) {
    }
    o.crash = 0;
    o.nodes[kCdcHoldingRoot].digest = 99;
    assert(recover(o) == OrdinaryTransactionResult::InvalidInstalled);
    assert(o.marker && o.exists(kCdcHoldingRoot));
  }
  puts(
      "CDC lineage: both-root leases, strict versions, bound intent, every rename restart, unknown-data refusal and "
      "partial cleanup PASS");
}
