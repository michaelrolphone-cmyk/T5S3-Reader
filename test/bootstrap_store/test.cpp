#include "runtime/drivers/BootstrapModuleStore.h"
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageExecutableAdmission.h"
#include <openssl/evp.h>
#include <cstdio>
#include <cstring>
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
bool registerBootstrapPackage(const RuntimePackages::ManagerProviderCandidateV2& c) {
 if(!c.elfBytes || c.elfLength<52 || memcmp(c.elfBytes,"\177ELF",4)) return false;
 ++registrations;printf("REGISTER %s %s bytes=%zu\n",c.driverId,c.provides,c.elfLength);return true;
}
}
int main(int argc,char**argv) {
 if(argc!=3)return 2;
 bool ok=RuntimeInstalledProviders::loadBootstrapPackages(argv[1],argv[2]);
 printf("RESULT %d registrations=%u\n",ok,registrations);return ok?0:1;
}
