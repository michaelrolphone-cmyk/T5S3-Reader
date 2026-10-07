#include "runtime/drivers/BootstrapModuleStore.h"
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageExecutableAdmission.h"
#include <openssl/evp.h>
#include <cstdio>
#include <cstring>
#include <fstream>
static unsigned registrations;
namespace RuntimePackages {
bool preflightCapturedPackage(const OrdinaryPackagePlan& p,const PackageRuntimePolicy& policy) {
 return preflightOrdinaryPackage(p,policy,[](const char*)->uint32_t{return UINT32_MAX;})==PreflightResult::ReadyForContentVerification;
}
bool declaredPackageSnapshot(const OrdinaryPackagePlan& p,const char* name,const uint8_t* bytes,size_t n) {
 for(size_t i=0;i<p.entryCount;++i) if(!strcmp(p.entries[i].name,name)) {
  unsigned length=0;uint8_t digest[32];
  return n==p.entries[i].sizeBytes && EVP_Digest(bytes,n,digest,&length,EVP_sha256(),nullptr)==1 && length==32 && ordinaryDigestEquals(digest,p.entries[i].sha256);
 }
 return false;
}
}
namespace RuntimeInstalledProviders {
bool registerBootstrapPackage(const RuntimePackages::ManagerProviderCandidateV2& c,const char* packageRoot) {
 if(!packageRoot || std::string(packageRoot)!=std::string("/Drivers/")+c.driverId) return false;
 if(!c.elfBytes || c.elfLength<52 || memcmp(c.elfBytes,"\177ELF",4)) return false;
 ++registrations;printf("REGISTER %s %s bytes=%zu os_cpu_abi=%u\n",c.driverId,c.provides,c.elfLength,c.requiredOsCpuAbi);return true;
}
}
static bool hostRead(const std::string& path,size_t limit,std::vector<uint8_t>& bytes) {
 std::ifstream file(path,std::ios::binary|std::ios::ate);
 const auto size=file.tellg();
 if(!file || size<=0 || static_cast<uint64_t>(size)>limit)return false;
 bytes.resize(static_cast<size_t>(size));file.seekg(0);
 return bool(file.read(reinterpret_cast<char*>(bytes.data()),size));
}
int main(int argc,char**argv) {
 if(argc!=3)return 2;
 bool ok=RuntimeInstalledProviders::loadBootstrapPackages(argv[1],argv[2],hostRead);
 printf("RESULT %d registrations=%u\n",ok,registrations);return ok?0:1;
}
