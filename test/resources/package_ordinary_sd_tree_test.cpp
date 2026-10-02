#include "runtime/packages/PackageOrdinarySdTree.h"
#include <cassert>
#include <cstdio>
using namespace RuntimePackages;
OrdinaryPackagePlan makePlan(){
  OrdinaryPackagePlan p{};p.schemaVersion=2;p.entryCount=2;
  std::strcpy(p.entries[0].name,"app.elf");std::strcpy(p.entries[1].name,"assets/fonts/body.bin");return p;
}
void reset(){
  assert(treeSd.handles==0);treeSd={};
  treeSd.nodes={{"/stage",true},{"/stage/.package.json",false},{"/stage/app.elf",false},
    {"/stage/assets",true},{"/stage/assets/fonts",true},{"/stage/assets/fonts/body.bin",false}};
}
int main(){
  const auto p=makePlan();reset();
  {OrdinarySdTreeOps ops("/stage");assert(ordinaryTreeInventory(p,ops,true));assert(treeSd.handles==0);}
  for(const char* path:{"/stage","/stage/assets","/stage/assets/fonts"}){
    reset();treeSd.readFailure=path;const auto before=treeSd.nodes;
    OrdinarySdTreeOps ops("/stage");assert(!purgeOrdinaryTree(p,ops,false));
    assert(treeSd.nodes==before&&treeSd.mutations.empty()&&treeSd.handles==0);
  }
  reset();treeSd.closeFailure="/stage/assets/fonts/body.bin";
  {OrdinarySdTreeOps ops("/stage");assert(!ordinaryTreeInventory(p,ops,true));assert(treeSd.handles==0);}
  reset();treeSd.nodes["/stage/assets/fonts/owner.txt"]=false;
  {OrdinarySdTreeOps ops("/stage");assert(!purgeOrdinaryTree(p,ops,false));assert(treeSd.mutations.empty());}
  reset();
  {OrdinarySdTreeOps ops("/stage");assert(purgeOrdinaryTree(p,ops,false));assert(treeSd.nodes.empty());}
  reset();treeSd.nodes={{"/stage",true}};
  {OrdinarySdTreeOps ops("/stage");assert(ops.createParents("assets/fonts/body.bin"));
    assert(treeSd.nodes.count("/stage/assets/fonts"));assert(!ops.createParents("../escape/file"));}
  reset();treeSd.nodes={{"/stage",true},{"/stage/assets",false}};
  {OrdinarySdTreeOps ops("/stage");assert(!ops.createParents("assets/fonts/body.bin"));assert(treeSd.handles==0);}
  reset();treeSd.nodes={{"/stage",true}};treeSd.createFailure="/stage/assets/fonts";
  {OrdinarySdTreeOps ops("/stage");assert(!ops.createParents("assets/fonts/body.bin"));
    assert(purgeOrdinaryTree(p,ops,true));assert(treeSd.nodes.empty());}
  std::puts("Production SD tree operations: read/close/create faults, unknown data, parent ownership and handle cleanup PASS");
}
