// Reuse production installer adapters and real OpenSSL SHA fixtures.
#define main ordinary_installer_fixture_main
#include "package_ordinary_installer_test.cpp"
#undef main
#include "runtime/packages/PackageOrdinaryManagedInstall.h"
struct ResourceDirectory {
  const Stage& stage;
  bool readManifest(char* out,size_t cap,size_t& used) const {
    used=stage.manifest.size();if(!used||used>cap)return false;
    std::memcpy(out,stage.manifest.data(),used);return true;
  }
  bool exactEntries(const OrdinaryPackagePlan& plan) const {
    if(stage.files.size()!=plan.entryCount)return false;
    for(size_t i=0;i<plan.entryCount;++i)if(!stage.files.count(plan.entries[i].name))return false;
    return true;
  }
  bool entrySize(const char* name,uint64_t& size) const {
    auto it=stage.files.find(name);if(it==stage.files.end())return false;size=it->second.size();return true;
  }
  bool readAt(const char* name,uint64_t offset,uint8_t* out,size_t size) const {
    auto it=stage.files.find(name);if(it==stage.files.end()||offset>it->second.size()||size>it->second.size()-offset)return false;
    std::memcpy(out,it->second.data()+offset,size);return true;
  }
};
int main(){
  Source source;source.files["guide.txt"]=std::vector<uint8_t>(1100,0x42);
  const std::string sha=digest(source.files.at("guide.txt"));
  const std::string json="{\"schema\":3,\"payload\":\"resources\",\"kind\":\"service\",\"id\":\"test\",\"version\":\"2.0.0\","
      "\"artifact\":null,\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,\"entries\":[{\"name\":\"guide.txt\","
      "\"size_bytes\":1100,\"sha256\":\""+sha+"\",\"executable\":false}],\"requires\":[],\"resource_imports\":[]}";
  OrdinaryPackagePlan plan{};assert(parseOrdinaryManifest(json.data(),json.size(),plan));
  const auto p=paths(Kind::Service);
  for(int failure=0;failure<=2;++failure){
    Storage disk;Stage stage(disk);Hash hash;uint8_t io[kOrdinaryIoBytes]{};Identity observed{},prior{};
    assert(makeResourceIdentity(Kind::Service,"test","1.0.0",&prior));
    disk.directories[p.target]={prior,true};disk.failRename=failure?failure:-1;
    auto verify=[&](const char* path,Identity& identity){return disk.verify(path,identity);};
    auto purge=[&](const char* path){return disk.purge(path);};
    auto installed=installCanonicalOrdinaryPackage(json.data(),json.size(),source,stage,hash,resolver,kPolicy,io,disk,verify,purge,true);
    if(failure){
      assert(installed.result!=OrdinaryInstallResult::Installed);
      assert(disk.verify(p.target,observed)&&!std::strcmp(observed.version,"1.0.0"));
      disk.failRename=-1;
      assert(publishOrdinaryPackage(disk,plan.identity,verify,purge,observed)==OrdinaryTransactionResult::Published);
    }else assert(installed.result==OrdinaryInstallResult::Installed);
    assert(disk.verify(p.target,observed)&&resourceOnly(observed)&&!observed.artifact[0]);
    ResourceDirectory retained{stage};
    assert(verifyCanonicalOrdinaryDirectory(retained,hash,resolver,kPolicy,io,observed)&&resourceOnly(observed));
    stage.files["guide.txt"][0]^=1;
    assert(!verifyCanonicalOrdinaryDirectory(retained,hash,resolver,kPolicy,io,observed));
    stage.files["guide.txt"][0]^=1;
    stage.files["owner.txt"]={1};
    assert(!verifyCanonicalOrdinaryDirectory(retained,hash,resolver,kPolicy,io,observed));
    stage.files.erase("owner.txt");
    auto& gate=systemPackageUseGate();assert(gate.pin(p.target));
    assert(uninstallOrdinaryPackage(disk,Kind::Service,"test",verify,purge,observed)==OrdinaryTransactionResult::InUse);
    assert(gate.unpin(p.target));disk.failPurge=true;
    assert(uninstallOrdinaryPackage(disk,Kind::Service,"test",verify,purge,observed)!=OrdinaryTransactionResult::Removed);
    assert(disk.exists(p.removing));disk.failPurge=false;
    assert(recoverOrdinaryPackage(disk,Kind::Service,"test",verify,purge,observed)==OrdinaryTransactionResult::Removed);
    assert(!disk.exists(p.target)&&!disk.exists(p.removing));
  }
  std::puts("Resource-only common installer: real SHA/readback, upgrade rollback/retry, exact inventory, use pins and interrupted removal PASS");
}
