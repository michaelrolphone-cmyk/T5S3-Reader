#include "runtime/packages/PackageOrdinaryTree.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace RuntimePackages;
struct Ops {
  std::map<std::string, bool> nodes;
  std::vector<std::string> removed, visited;
  std::string failVisit;
  std::string repeatBasename;
  int failRemove = -1;
  bool exists(const char* path) { return nodes.count(path); }
  template<class Visitor> bool visit(const char* path, Visitor visitor) {
    visited.emplace_back(path);
    if (path == failVisit || !exists(path) || !nodes[path]) return false;
    const std::string prefix = path[0] ? std::string(path) + "/" : "";
    for (const auto& node : nodes) {
      if (node.first.empty() || node.first.compare(0, prefix.size(), prefix)) continue;
      const auto suffix = node.first.substr(prefix.size());
      if (suffix.find('/') != std::string::npos || suffix.empty()) continue;
      if (!visitor(suffix.c_str(), node.second)) return false;
      if (suffix == repeatBasename && !visitor(suffix.c_str(), node.second)) return false;
    }
    return true;
  }
  bool remove(const char* path) {
    if (failRemove == 0) return false;
    if (failRemove > 0) --failRemove;
    if (!exists(path) || nodes[path]) return false;
    removed.emplace_back(path); nodes.erase(path); return true;
  }
  bool rmdir(const char* path) {
    if (!exists(path) || !nodes[path]) return false;
    const std::string prefix = path[0] ? std::string(path) + "/" : "";
    for (const auto& node : nodes)
      if (node.first != path && !node.first.compare(0, prefix.size(), prefix)) return false;
    removed.emplace_back(path); nodes.erase(path); return true;
  }
};
OrdinaryPackagePlan plan() {
  OrdinaryPackagePlan p{};p.schemaVersion=2;p.entryCount=3;
  std::strcpy(p.entries[0].name,"app.elf");
  std::strcpy(p.entries[1].name,"assets/fonts/body.bin");
  std::strcpy(p.entries[2].name,"assets/images/logo.bin");
  return p;
}
Ops fixture() {
  Ops o; o.failVisit = "nonexistent";
  o.nodes = {{"",true},{".package.json",false},{"app.elf",false},
    {"assets",true},{"assets/fonts",true},{"assets/fonts/body.bin",false},
    {"assets/images",true},{"assets/images/logo.bin",false}};
  return o;
}
int main() {
  const auto p=plan(); OrdinaryTreeLayout layout{};
  assert(ordinaryTreeLayout(p,layout) && layout.count==3);
  auto good=fixture();assert(ordinaryTreeInventory(p,good,true));
  assert(purgeOrdinaryTree(p,good,false) && good.nodes.empty());
  assert(good.removed[good.removed.size()-2]==".package.json");
  for (const std::string unknown : {"owner.txt","assets/fonts/owner.txt","assets/private"}) {
    auto o=fixture();o.nodes[unknown]=unknown=="assets/private";
    const auto before=o.nodes;
    assert(!purgeOrdinaryTree(p,o,false));assert(o.nodes==before && o.removed.empty());
  }
  for (const std::string directory : {"", "assets", "assets/fonts", "assets/images"}) {
    auto o=fixture();o.failVisit=directory;const auto before=o.nodes;
    assert(!purgeOrdinaryTree(p,o,false) && o.nodes==before && o.removed.empty());
  }
  auto badType=fixture();badType.nodes["assets/fonts"]=false;
  assert(!ordinaryTreeInventory(p,badType,true));
  auto missing=fixture();missing.nodes.erase("assets/fonts/body.bin");
  assert(!ordinaryTreeInventory(p,missing,true));
  assert(ordinaryTreeInventory(p,missing,false));
  // Every file-deletion interruption retains metadata and can resume safely.
  for (int failure=0;failure<4;++failure) {
    auto o=fixture();o.failRemove=failure;
    assert(!purgeOrdinaryTree(p,o,false));assert(o.nodes.count(".package.json"));
    o.failRemove=-1;assert(purgeOrdinaryTree(p,o,false) && o.nodes.empty());
  }
  auto partial=fixture();partial.nodes.erase(".package.json");partial.nodes.erase("assets/fonts/body.bin");
  const auto saved=partial.nodes;
  assert(!purgeOrdinaryTree(p,partial,false) && partial.nodes==saved);
  assert(purgeOrdinaryTree(p,partial,true) && partial.nodes.empty());
  OrdinaryPackagePlan empty{};Ops tombstone;tombstone.failVisit="absent";tombstone.nodes[""]=true;
  assert(purgeOrdinaryTree(empty,tombstone,false));
  tombstone.nodes={{"",true},{"owner.txt",false}};
  assert(!purgeOrdinaryTree(empty,tombstone,false) && tombstone.nodes.count("owner.txt"));
  // Finder metadata can accompany copied installed files without supplying
  // any real member or deletion authority. Namespace/type matching is exact.
  auto copied=fixture();
  for (const char* name : {"._.package.json", "._app.elf", "._assets", "assets/._fonts",
                          "assets/._images", "assets/fonts/._body.bin", "assets/images/._logo.bin",
                          ".DS_Store", "assets/.DS_Store", "assets/fonts/.DS_Store", "assets/images/.DS_Store"})
    copied.nodes[name]=false;
  const auto inspect=[&](Ops& o) {
    return ordinaryTreeInventory(p,o,true,true,OrdinaryCopyMetadata::InspectInstalled);
  };
  const auto original=copied.nodes;
  assert(inspect(copied));
  assert(!ordinaryTreeInventory(p,copied,true,true));
  assert(!ordinaryTreeInventory(p,copied,false,true,OrdinaryCopyMetadata::InspectInstalled));
  assert(!purgeOrdinaryTree(p,copied,false,true));
  assert(copied.nodes==original && copied.removed.empty());
  for (const char* required : {".package.json", "app.elf", "assets/fonts/body.bin", "assets/images/logo.bin"}) {
    auto o=copied;o.nodes.erase(required);assert(!inspect(o));
  }
  for (const char* name : {"._unknown", "._App.elf", "._app.elf.exe", "owner.txt",
                          "assets/._owner.txt", ".hidden", "assets/.ds_store"}) {
    auto o=copied;o.nodes[name]=false;assert(!inspect(o));
  }
  for (const char* name : {"._app.elf", "._assets", ".DS_Store", "assets/fonts/._body.bin"}) {
    auto o=copied;o.nodes[name]=true;assert(!inspect(o));
  }
  for (const char* repeated : {"._.package.json", "._assets", "._body.bin", ".DS_Store", "app.elf"}) {
    auto o=copied;o.repeatBasename=repeated;assert(!inspect(o));
  }
  copied.nodes[kPackageReceiptName]=false;
  copied.nodes[std::string("._")+kPackageReceiptName]=false;
  assert(inspect(copied));
  assert(!ordinaryTreeInventory(p,copied,true,false,OrdinaryCopyMetadata::InspectInstalled));
  std::puts("Installed copy metadata: exact inert companions, genuine members, aliases/types/duplicates and strict purge PASS");
  std::puts("Declared resource tree: bounded inventory, unknown preservation, read faults, manifest-last cleanup and restart PASS");
}
