#include "runtime/packages/PackageOrdinaryStageRecovery.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>

using namespace RuntimePackages;
namespace {
struct Node {
  Identity identity{};
  bool valid = true;
  bool unknown = false;
  bool purgeFails = false;
};
struct Disk {
  std::map<std::string, Node> nodes;
  unsigned renames = 0;
  unsigned purges = 0;
  bool exists(const char* path) { return nodes.count(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to)) return false;
    auto node = nodes.extract(from);
    node.key() = to;
    nodes.insert(std::move(node));
    ++renames;
    return true;
  }
  bool verify(const char* path, Identity& observed) {
    observed = {};
    auto item = nodes.find(path);
    if (item == nodes.end() || !item->second.valid || item->second.unknown) return false;
    observed = item->second.identity;
    return true;
  }
  bool known(const char* path) {
    auto item = nodes.find(path);
    return item != nodes.end() && !item->second.unknown;
  }
  bool purge(const char* path) {
    auto item = nodes.find(path);
    if (item == nodes.end() || item->second.unknown || item->second.purgeFails) return false;
    nodes.erase(item);
    ++purges;
    return true;
  }
};
Identity make(Kind kind, const char* version) {
  Identity id{};
  assert(makeIdentity(kind, "stage-recovery", version, "module.elf", false, &id));
  return id;
}
struct Fixture {
  explicit Fixture(Kind k) : kind(k) {
    assert(ordinaryTransactionPaths(kind, "stage-recovery", paths));
  }
  Kind kind;
  OrdinaryTransactionPaths paths{};
  Disk disk;
  Identity observed{};
  void stage(const char* version, bool valid = true) {
    disk.nodes[paths.stage] = {make(kind, version), valid, false, false};
  }
  void target(const char* version) {
    disk.nodes[paths.target] = {make(kind, version), true, false, false};
  }
  OrdinaryStageReview review() {
    return reviewOrdinaryStage(disk, kind, "stage-recovery",
      [this](const char* path, Identity& identity) { return disk.verify(path, identity); });
  }
  OrdinaryTransactionResult retry() {
    return retryOrdinaryStage(disk, kind, "stage-recovery",
      [this](const char* path, Identity& identity) { return disk.verify(path, identity); },
      [this](const char* path) { return disk.purge(path); }, observed);
  }
  OrdinaryStageDiscardResult discard() {
    return discardOrdinaryStage(disk, kind, "stage-recovery",
      [this](const char* path) { return disk.known(path); },
      [this](const char* path) { return disk.purge(path); });
  }
};
void exercise(Kind kind) {
  Fixture f(kind);
  assert(f.review().state == OrdinaryStageState::Missing);
  assert(f.discard() == OrdinaryStageDiscardResult::NoStage);
  assert(f.retry() == OrdinaryTransactionResult::InvalidStage);
  f.target("1.0.0");
  f.stage("1.0.1");
  const unsigned before = f.disk.renames;
  const auto review = f.review();
  assert(review.state == OrdinaryStageState::Ready);
  assert(std::strcmp(review.installed.version, "1.0.0") == 0);
  assert(std::strcmp(review.candidate.version, "1.0.1") == 0);
  assert(f.disk.renames == before); // Inspection must never mutate SD.
  assert(f.retry() == OrdinaryTransactionResult::Published);
  assert(std::strcmp(f.disk.nodes.at(f.paths.target).identity.version, "1.0.1") == 0);
  assert(!f.disk.exists(f.paths.stage) && !f.disk.exists(f.paths.backup));
  f.stage("1.0.0");
  assert(f.review().state == OrdinaryStageState::StaleVersion);
  assert(f.retry() == OrdinaryTransactionResult::VersionRejected);
  assert(f.disk.exists(f.paths.stage));
  assert(f.discard() == OrdinaryStageDiscardResult::Discarded);
  assert(f.disk.exists(f.paths.target));
  f.stage("1.0.2", false); // A partial/corrupt stage cannot be retried.
  assert(f.review().state == OrdinaryStageState::InvalidStage);
  assert(f.retry() == OrdinaryTransactionResult::InvalidStage);
  f.disk.nodes.at(f.paths.stage).unknown = true;
  assert(f.discard() == OrdinaryStageDiscardResult::UnknownEntries);
  assert(f.disk.exists(f.paths.stage));
  f.disk.nodes.at(f.paths.stage).unknown = false;
  f.disk.nodes.at(f.paths.stage).purgeFails = true;
  assert(f.discard() == OrdinaryStageDiscardResult::CleanupFailed);
  assert(f.disk.exists(f.paths.stage));
  f.disk.nodes.at(f.paths.stage).purgeFails = false;
  assert(f.discard() == OrdinaryStageDiscardResult::Discarded);
  assert(f.disk.exists(f.paths.target));
  f.stage("1.0.2");
  assert(systemPackageUseGate().pin(f.paths.target));
  assert(f.review().state == OrdinaryStageState::InUse);
  assert(f.retry() == OrdinaryTransactionResult::InUse);
  assert(f.discard() == OrdinaryStageDiscardResult::InUse);
  assert(f.disk.exists(f.paths.stage) && f.disk.exists(f.paths.target));
  assert(systemPackageUseGate().unpin(f.paths.target));
  f.disk.nodes[f.paths.backup] = {make(kind, "1.0.0"), true, false, false};
  assert(f.review().state == OrdinaryStageState::RecoveryRequired);
  assert(f.retry() == OrdinaryTransactionResult::AmbiguousState);
  assert(f.discard() == OrdinaryStageDiscardResult::RecoveryRequired);
  assert(f.disk.exists(f.paths.stage) && f.disk.exists(f.paths.backup));
  f.disk.nodes.erase(f.paths.backup);
  f.disk.nodes[f.paths.removing] = {make(kind, "1.0.0"), true, false, false};
  assert(f.discard() == OrdinaryStageDiscardResult::RecoveryRequired);
  f.disk.nodes.erase(f.paths.removing);
  f.disk.nodes.at(f.paths.target).valid = false;
  assert(f.review().state == OrdinaryStageState::InvalidInstalled);
  assert(f.retry() == OrdinaryTransactionResult::InvalidInstalled);
  assert(f.disk.exists(f.paths.stage) && f.disk.exists(f.paths.target));
  f.disk.nodes.at(f.paths.target).valid = true;
  assert(f.discard() == OrdinaryStageDiscardResult::Discarded);
}
} // namespace

int main() {
  exercise(Kind::Application);
  exercise(Kind::Driver);
  exercise(Kind::Service);
  exercise(Kind::Provider);
  Disk disk;
  assert(reviewOrdinaryStage(disk, Kind::Driver, "../escape",
    [&](const char* path, Identity& id) { return disk.verify(path, id); }).state ==
    OrdinaryStageState::InvalidIdentity);
  assert(discardOrdinaryStage(disk, Kind::Driver, "../escape",
    [&](const char* path) { return disk.known(path); },
    [&](const char* path) { return disk.purge(path); }) ==
    OrdinaryStageDiscardResult::InvalidIdentity);
  std::puts("Ordinary stage recovery: four-kind inspect/retry/discard, stale, corrupt, unknown, pinned and interrupted states PASS");
}
