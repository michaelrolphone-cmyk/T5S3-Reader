#include "runtime/packages/PackagePairTransaction.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>

using namespace RuntimePackages;
namespace {
constexpr PairPaths p{"target.elf", "target.json", "stage.elf", "stage.json",
                      "backup.elf", "backup.json"};
struct Disk {
  std::map<std::string, std::string> files;
  int operations = 0;
  int failAt = -1;
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (++operations == failAt || !exists(from) || exists(to)) return false;
    files[to] = files.at(from);
    files.erase(from);
    return true;
  }
  bool remove(const char* path) {
    if (++operations == failAt) return false;
    return files.erase(path) == 1;
  }
  bool verify(const char* elf, const char* manifest) const {
    const auto a = files.find(elf), b = files.find(manifest);
    return a != files.end() && b != files.end() && a->second == b->second &&
           (a->second == "old" || a->second == "new");
  }
  bool recover() {
    return recoverPairTransaction(*this, p,
      [this](const char* a, const char* b) { return verify(a,b); });
  }
  bool publish(bool allowed = true) {
    return publishPairTransaction(*this, p,
      [this](const char* a, const char* b) { return verify(a,b); }, allowed);
  }
  void old() { files[p.targetElf] = files[p.targetManifest] = "old"; }
  void stage() { files[p.stageElf] = files[p.stageManifest] = "new"; }
};
void basic() {
  Disk fresh; fresh.stage(); assert(fresh.publish());
  assert(fresh.verify(p.targetElf,p.targetManifest));
  assert(!fresh.exists(p.backupElf) && !fresh.exists(p.backupManifest));
  Disk upgrade; upgrade.old(); upgrade.stage(); assert(upgrade.publish());
  assert(upgrade.files.at(p.targetElf) == "new");
  assert(!upgrade.exists(p.backupElf) && !upgrade.exists(p.backupManifest));
}
void failures() {
  // Interrupt at every rename/remove; recovery must preserve a complete
  // old OR new pair and must never accept mismatched files.
  for (int i = 1; i <= 8; ++i) {
    Disk d; d.old(); d.stage(); d.failAt = i;
    (void)d.publish(); d.failAt = -1;
    assert(d.recover()); assert(d.verify(p.targetElf,p.targetManifest));
  }
  for (int i = 1; i <= 4; ++i) {
    Disk d; d.stage(); d.failAt = i;
    (void)d.publish(); d.failAt = -1;
    assert(d.recover());
    if (d.exists(p.targetElf)) assert(d.verify(p.targetElf,p.targetManifest));
  }
}
void refusesUnsafe() {
  Disk busy; busy.old(); busy.stage();
  assert(!busy.publish(false) && busy.operations == 0);
  Disk corrupt; corrupt.old(); corrupt.stage();
  corrupt.files[p.backupElf] = "bad"; corrupt.files[p.backupManifest] = "old";
  assert(!corrupt.publish()); assert(corrupt.files.at(p.targetElf) == "old");
  Disk badTarget; badTarget.old(); badTarget.stage();
  badTarget.files[p.targetElf] = "bad";
  assert(!badTarget.publish()); assert(badTarget.operations == 0);
  Disk orphan; orphan.files[p.targetManifest] = "old";
  assert(!orphan.recover());
  assert(!validPairPaths({"one", "one", "three", "four", "five", "six"}));
}
void recoveryCases() {
  Disk afterFirstBackup; afterFirstBackup.files[p.backupElf] = "old";
  afterFirstBackup.files[p.targetManifest] = "old";
  assert(afterFirstBackup.recover());
  assert(afterFirstBackup.verify(p.targetElf,p.targetManifest));
  Disk restoring; restoring.files[p.targetElf] = "old";
  restoring.files[p.backupManifest] = "old";
  assert(restoring.recover()); assert(restoring.verify(p.targetElf,p.targetManifest));
  Disk cleanup; cleanup.files[p.targetElf] = cleanup.files[p.targetManifest] = "new";
  cleanup.files[p.backupManifest] = "old";
  assert(cleanup.recover()); assert(!cleanup.exists(p.backupManifest));
  Disk incomplete; incomplete.files[p.targetElf] = "new";
  incomplete.files[p.stageManifest] = "new";
  assert(incomplete.recover()); assert(incomplete.verify(p.targetElf,p.targetManifest));
  Disk broken; broken.files[p.targetElf] = "bad";
  broken.files[p.backupElf] = "old"; broken.files[p.backupManifest] = "old";
  broken.failAt = 1; assert(!broken.recover());
  assert(broken.verify(p.backupElf,p.backupManifest));
  broken.failAt = -1; assert(broken.recover());
  assert(broken.verify(p.targetElf,p.targetManifest));
}
}
int main() {
  basic(); failures(); refusesUnsafe(); recoveryCases();
  std::puts("Legacy app pair transaction: rename/purge faults, rollback, orphan refusal passed");
}
