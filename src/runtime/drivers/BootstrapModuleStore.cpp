#include "runtime/packages/ProviderAbiProfile.h"
#include "BootstrapModuleStore.h"
#include "InstalledProviderGraph.h"
#include "DeviceProviderExecutorV2.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageExecutableAdmission.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace RuntimeInstalledProviders {
namespace {
using namespace RuntimePackages;
constexpr size_t kJsonLimit=65536, kElfLimit=1024u*1024u;
constexpr PackageRuntimePolicy policy{"xtensa-esp32s3",2,kElfLimit,16u*1024u*1024u};
bool relative(const char* path) {
  if (!path || !*path || std::strlen(path)>192 || *path=='/') return false;
  const char* start=path;
  for(const char* p=path;;++p) {
    if (*p=='\\' || (static_cast<unsigned char>(*p)<32 && *p)) return false;
    if (*p=='/' || !*p) {
      const size_t n=p-start;
      if(!n || (n==1 && start[0]=='.') || (n==2 && start[0]=='.' && start[1]=='.')) return false;
      if(!*p) return true;
      start=p+1;
    }
  }
}
// One descriptor at a time (below the four-descriptor bootstrap budget).
// Bytes are owned before parsing/relocation; bounded reads yield on time/work.
BootPackageReader packageReader=nullptr;
size_t admittedBytes=0;
uint32_t loadBegan=0;
bool read(const std::string& path,size_t limit,std::vector<uint8_t>& bytes) {
  if(packageReader && millis()-loadBegan<45000u)
    return packageReader(path,limit,bytes) && millis()-loadBegan<45000u;
  return false; // No implicit POSIX/internal-flash storage route.
}

bool json(const std::string& path,JsonDocument& out) {
  std::vector<uint8_t> bytes;
  return read(path,kJsonLimit,bytes) &&
      RuntimePackages::safePackageJsonObject(reinterpret_cast<const char*>(bytes.data()),bytes.size()) &&
      !deserializeJson(out,bytes.data(),bytes.size());
}
bool imports(std::vector<uint8_t>& bytes,const char* (&names)[128],size_t& count) {
  count=0;
  if(bytes.size()==1 && bytes[0]=='\n') return true;
  size_t start=0;
  for(size_t i=0;i<bytes.size();++i) {
    if(bytes[i]=='\n') {
      if(i==start || i-start>=128 || count==128) return false;
      bytes[i]=0; const char* name=reinterpret_cast<char*>(bytes.data()+start);
      if(count && std::strcmp(names[count-1],name)>=0) return false;
      names[count++]=name; start=i+1;
    } else if(bytes[i]<=32 || bytes[i]>126) return false;
  }
  return count && start==bytes.size();
}
bool rejected(const char* path,int line) {
  LOG_ERR("SDBOOT","Rejected external package %s validation line=%d",path,line);
  return false;
}
bool add(const std::string& manifestPath) {
  JsonDocument manifest;
  if(!json(manifestPath,manifest)) return rejected(manifestPath.c_str(),__LINE__);
  const char* id=manifest["id"] | "";
  const char* version=manifest["version"] | "";
  const char* filename=manifest["file_name"] | "";
  if(std::strcmp(manifest["type"] | "","driver") || !safeId(id) ||
     !safeArtifact(filename) || std::strchr(filename,'/') || manifest["driver_abi"]!=2 ||
     std::strcmp(manifest["architecture"] | "","xtensa-esp32s3") ||
     !manifest["provides"].is<JsonArrayConst>() || manifest["provides"].size()!=1) return rejected(manifestPath.c_str(),__LINE__);
  const std::string directory=manifestPath.substr(0,manifestPath.find_last_of('/'));
  std::vector<uint8_t> metadata;
  std::unique_ptr<OrdinaryPackagePlan> plan(new(std::nothrow) OrdinaryPackagePlan{});
  if(!plan || !read(directory+"/.package.json",4096,metadata) ||
     !parseOrdinaryManifest(reinterpret_cast<const char*>(metadata.data()),metadata.size(),*plan) ||
     plan->identity.kind!=Kind::Driver || std::strcmp(plan->identity.id,id) ||
     std::strcmp(plan->identity.version,version) || std::strcmp(plan->identity.artifact,filename) ||
     !preflightCapturedPackage(*plan,policy)) return rejected(manifestPath.c_str(),__LINE__);
  uint32_t osCpuAbi=1;
  if(!manifest["os_cpu_abi"].isUnbound()) {
    if(!manifest["os_cpu_abi"].is<uint32_t>()) return rejected(manifestPath.c_str(),__LINE__);
    osCpuAbi=manifest["os_cpu_abi"].as<uint32_t>();
  }
  if(osCpuAbi!=1 && osCpuAbi!=2 && osCpuAbi!=3) return rejected(manifestPath.c_str(),__LINE__);
  const char* capability=manifest["provides"][0]["capability"] | "";
  const uint32_t api=manifest["provides"][0]["api"] | 0u;
  if(!safePackageCapability(capability) || !api || !manifest["requires"].is<JsonArrayConst>() ||
     manifest["requires"].size()!=plan->requirementCount) return rejected(manifestPath.c_str(),__LINE__);
  RuntimeProviders::RequirementV2 needs[kMaxPackageRequirements]{};
  for(size_t i=0;i<plan->requirementCount;++i) {
    if(std::strcmp(manifest["requires"][i]["capability"] | "",plan->requirements[i].capability) ||
       (manifest["requires"][i]["api"] | 0u)!=plan->requirements[i].minApi) return rejected(manifestPath.c_str(),__LINE__);
    needs[i]={plan->requirements[i].capability,plan->requirements[i].minApi};
  }
  std::vector<uint8_t> elf, importBytes, bytes;
  bool driverManifest=false, profile=false;
  // Validate every declared file, including the driver manifest and imports.
  // No package's hash confers authority; executor/loader enforce import policy.
  for(size_t i=0;i<plan->entryCount;++i) {
    const auto& entry=plan->entries[i];
    if(!read(directory+"/"+entry.name,entry.sizeBytes,bytes) ||
       !declaredPackageSnapshot(*plan,entry.name,bytes.data(),bytes.size())) return rejected(manifestPath.c_str(),__LINE__);
    if(!std::strcmp(entry.name,filename)) elf=bytes;
    if(!std::strcmp(entry.name,"privileged-imports.v1")) importBytes=bytes;
    if(!std::strcmp(entry.name,"manifest.json")) {
      std::vector<uint8_t> original;
      if(!read(manifestPath,kJsonLimit,original) || original!=bytes) return rejected(manifestPath.c_str(),__LINE__);
      driverManifest=true;
    }
    if(!std::strcmp(entry.name,"provider-abi.v1")) {
      const std::string expected="os-cpu-abi="+std::to_string(osCpuAbi)+"\nprovides="+std::string(capability)+"\napi="+std::to_string(api)+"\n";
      if(bytes.size()!=expected.size() || std::memcmp(bytes.data(),expected.data(),bytes.size())) return rejected(manifestPath.c_str(),__LINE__);
      profile=true;
    }
  }
  const char* symbols[128]{}; size_t count=0;
  if(!driverManifest || !profile || elf.empty() || !imports(importBytes,symbols,count)) return rejected(manifestPath.c_str(),__LINE__);
  ManagerProviderCandidateV2 candidate{};
  candidate.requiredOsCpuAbi=osCpuAbi;
  candidate.driverId=id;candidate.provides=capability;candidate.providesApi=api;
  candidate.requirements=needs;candidate.requirementCount=plan->requirementCount;
  candidate.elfBytes=elf.data();candidate.elfLength=elf.size();
  candidate.importedSymbols=symbols;candidate.importedSymbolCount=count;
  // Boot admission owns a verified snapshot before the SD controller handoff.
  // Resource imports remain disabled until provider-backed storage exists.
  // Pin the real SD generation through graph teardown, exactly like lazy loads.
  if(plan->resourceImportCount) return rejected(manifestPath.c_str(),__LINE__);
  const std::string packageRoot=std::string("/Drivers/")+id;
  if(elf.size()>2u*1024u*1024u-admittedBytes) return rejected(manifestPath.c_str(),__LINE__);
  if(!registerBootstrapPackage(candidate,packageRoot.c_str())) return rejected(manifestPath.c_str(),__LINE__);
  admittedBytes+=elf.size();
  LOG_INF("SDBOOT","registered origin=%s/%s id=%s version=%s bytes=%u",directory.c_str(),filename,id,version,(unsigned)elf.size());
  return true;
}
}
bool loadBootstrapPackages(const char* root,const char* expectedBoard,BootPackageReader reader) {
  packageReader=reader;
  admittedBytes=0;
  loadBegan=millis();
  if(!reader || !root || root[0]!='/' || std::strlen(root)>64 || !expectedBoard) return false;
  JsonDocument boot,board;
  if(!json(std::string(root)+"/System/Config/boot.json",boot)) return false;
  const char* boardPath=boot["board"] | "";
  if(!relative(boardPath) || !json(std::string(root)+"/"+boardPath,board) ||
     std::strcmp(board["board_id"] | "",expectedBoard) || !boot["drivers"].is<JsonArrayConst>() ||
     !boot["drivers"].size() || boot["drivers"].size()>16) return false;
  const uint32_t began=millis();
  // Register all packages before activating hardware. The graph resolves and
  // rejects missing/ambiguous dependencies and cycles through its normal path.
  for(JsonObjectConst selected:boot["drivers"].as<JsonArrayConst>()) {
    if(millis()-began>=45000u) return false;
    const char* path=selected["manifest"] | "";
    // Profile selects an ordinary installed ID, never an alternate copy or ELF.
    if(!relative(path) || std::strncmp(path,"Drivers/",8)) return false;
    const char* slash=std::strchr(path+8,'/');
    if(!slash || std::strcmp(slash,"/manifest.json")) return false;
    const std::string id(path+8,slash);
    if(!safeId(id.c_str())) return false;
    const std::string target=std::string(root)+"/Drivers/"+id;
    JsonDocument selectedManifest;
    if(!json(target+"/manifest.json",selectedManifest) ||
       std::strcmp(selectedManifest["id"] | "",id.c_str()) ||
       !add(target+"/manifest.json")) return false;
    delay(1);
  }
  return true;
}
}
