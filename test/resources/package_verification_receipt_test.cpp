#include <cassert>
#include <cstdio>

#include "runtime/packages/PackageOrdinarySdTree.h"
#include "runtime/packages/PackageVerificationReceiptSd.h"
using namespace RuntimePackages;
namespace {
bool generationOkay = true;
uint8_t nextGeneration = 1;
}  // namespace
namespace RuntimePackages {
bool newPackageReceiptGeneration(uint8_t (&out)[16]) {
  if (!generationOkay) return false;
  std::memset(out, nextGeneration++, sizeof(out));
  return true;
}
}  // namespace RuntimePackages
OrdinaryPackagePlan planFor(Kind kind, bool resources = false) {
  OrdinaryPackagePlan plan{};
  plan.schemaVersion = resources ? 3 : 2;
  plan.entryCount = 1;
  if (resources)
    assert(makeResourceIdentity(kind, "reference", "1.2.3", &plan.identity));
  else
    assert(makeIdentity(kind, "reference", "1.2.3", "module.elf", false, &plan.identity));
  auto& entry = plan.entries[0];
  std::strcpy(entry.name, resources ? "help.txt" : "module.elf");
  entry.sizeBytes = resources ? 3 : 64;
  entry.executable = !resources;
  std::memset(entry.sha256, 'a', 64);
  entry.sha256[64] = 0;
  return plan;
}
int main() {
  uint8_t digest[32]{}, generation[16]{};
  std::memset(digest, 0x42, 32);
  generation[0] = 1;
  for (auto kind : {Kind::Application, Kind::Driver, Kind::Service, Kind::Provider}) {
    auto plan = planFor(kind);
    PackageVerificationReceipt receipt{}, decoded{};
    assert(makePackageReceipt(plan, digest, generation, receipt));
    uint8_t bytes[kPackageReceiptBytes]{};
    assert(encodePackageReceipt(receipt, bytes));
    assert(decodePackageReceipt(bytes, sizeof(bytes), decoded) && receiptMatchesManifest(decoded, plan, digest));
    for (size_t n = 0; n < sizeof(bytes); ++n) assert(!decodePackageReceipt(bytes, n, decoded));
    bytes[10] = 1;
    assert(!decodePackageReceipt(bytes, sizeof(bytes), decoded));
    bytes[10] = 0;
    bytes[95] = 1;
    assert(!decodePackageReceipt(bytes, sizeof(bytes), decoded));
    bytes[95] = 0;
    assert(decodePackageReceipt(bytes, sizeof(bytes), decoded));
    digest[0] ^= 1;
    assert(!receiptMatchesManifest(decoded, plan, digest));
    digest[0] ^= 1;
    plan.entries[0].sizeBytes++;
    assert(!receiptMatchesManifest(decoded, plan, digest));
    plan.entries[0].sizeBytes--;
    std::strcpy(plan.identity.version, "1.2.4");
    assert(!receiptMatchesManifest(decoded, plan, digest));
  }
  auto plan = planFor(Kind::Service, true);
  PackageVerificationReceipt receipt{};
  assert(makePackageReceipt(plan, digest, generation, receipt));
  assert(!receipt.executableBytes && !receiptAny(receipt.executableSha256, 32));
  receipt.executableBytes = 52;
  assert(!validPackageReceipt(receipt));
  for (int failure = 0; failure < 4; ++failure) {
    assert(!treeSd.handles);
    treeSd = {};
    treeSd.nodes["/stage"] = true;
    generationOkay = failure != 3;
    if (failure == 1) treeSd.readFailure = "/stage/.package.receipt";
    if (failure == 2) treeSd.closeFailure = "/stage/.package.receipt";
    const auto result = writeVerifiedStageReceipt("/stage", plan, reinterpret_cast<const uint8_t*>("{}"), 2);
    assert(result == (failure == 2 ? ReceiptWriteResult::CloseUncertain
                      : failure    ? ReceiptWriteResult::Failed
                                   : ReceiptWriteResult::Complete));
    if (!failure) {
      assert(readPackageReceipt("/stage", plan, reinterpret_cast<const uint8_t*>("{}"), 2, receipt) ==
             ReceiptReadResult::Matched);
      assert(readPackageReceipt("/stage", plan, reinterpret_cast<const uint8_t*>("[]"), 2, receipt) ==
             ReceiptReadResult::Invalid);
    }
  }
  generationOkay = true;
  assert(!treeSd.handles);
  treeSd = {};
  treeSd.nodes = {{"/stage", true},
                  {"/stage/.package.json", false},
                  {"/stage/help.txt", false},
                  {"/stage/.package.receipt", false}};
  OrdinarySdTreeOps source("/stage");
  assert(!ordinaryTreeInventory(plan, source, true));
  OrdinarySdTreeOps managed("/stage");
  assert(ordinaryTreeInventory(plan, managed, true, true));
  treeSd.nodes["/stage/.package.receipt.extra"] = false;
  {
    OrdinarySdTreeOps ops("/stage");
    assert(!purgeOrdinaryTree(plan, ops, false, true));
    assert(treeSd.mutations.empty());
  }
  treeSd.nodes.erase("/stage/.package.receipt.extra");
  treeSd.nodes["/stage/.package.receipt"] = true;
  {
    OrdinarySdTreeOps ops("/stage");
    assert(!purgeOrdinaryTree(plan, ops, false, true));
    assert(treeSd.mutations.empty());
  }
  treeSd.nodes["/stage/.package.receipt"] = false;
  {
    OrdinarySdTreeOps ops("/stage");
    assert(purgeOrdinaryTree(plan, ops, false, true));
  }
  assert(treeSd.mutations.front() == "/stage/.package.receipt");
  assert(treeSd.mutations[treeSd.mutations.size() - 2] == "/stage/.package.json");
  treeSd = {};
  treeSd.nodes = {{"/stage", true}, {"/stage/.package.receipt", false}};
  OrdinaryPackagePlan empty{};
  {
    OrdinarySdTreeOps ops("/stage");
    assert(!purgeOrdinaryTree(empty, ops, false, true));
    assert(treeSd.mutations.empty());
  }
  {
    OrdinarySdTreeOps ops("/stage");
    assert(purgeOrdinaryTree(plan, ops, true, true));
  }
  puts(
      "Verification receipt: four-kind wire binding, explicit data-only mode, truncation/refusal, SD "
      "readback/uncertain-close and exact receipt-first/manifest-last ownership PASS");
}
