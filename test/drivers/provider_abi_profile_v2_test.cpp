#include "runtime/packages/ProviderAbiProfile.h"
#include <cassert>
#include <cstring>
#include <string>
using namespace RuntimePackages;
int main() {
 uint32_t revision=0,api=0;char capability[64];
 for(unsigned r=1;r<=2;++r) {
  std::string text="os-cpu-abi="+std::to_string(r)+"\nprovides=display.output\napi=1\n";
  assert(parseProviderAbiProfile(text.data(),text.size(),revision,capability,api));
  assert(revision==r && api==1 && !strcmp(capability,"display.output"));
  for(size_t n=0;n<text.size();++n)assert(!parseProviderAbiProfile(text.data(),n,revision,capability,api));
 }
 for(const char *bad:{"os-cpu-abi=3\nprovides=display.output\napi=1\n",
  "os-cpu-abi=02\nprovides=display.output\napi=1\n",
  "os-cpu-abi=2\nprovides=display.output\napi=4294967296\n",
  "os-cpu-abi=2\nprovides=display.output\napi=1\nextra"})
  assert(!parseProviderAbiProfile(bad,strlen(bad),revision,capability,api));
 for(const char *good:{"{}","{\"requires\":[{\"os_cpu_abi\":2}]}"}) {
  assert(providerManifestOsCpuAbi(good,strlen(good),revision));assert(revision==1);
 }
 const char *good="{\"id\":\"display\",\"os_cpu_abi\": 2 ,\"requires\":[]}";
 assert(providerManifestOsCpuAbi(good,strlen(good),revision) && revision==2);
 for(const char *bad:{"{\"os_cpu_abi\":true}","{\"os_cpu_abi\":\"2\"}",
  "{\"os_cpu_abi\":null}","{\"os_cpu_abi\":3}","{\"os_cpu_abi\":2.0}",
  "{\"os_cpu_abi\":2,\"os_cpu_abi\":1}","{\"os_cpu_abi\":2,\"bad\":}",
  "{\"nested\":{\"a\":1,\"a\":2},\"os_cpu_abi\":2}"})
  assert(!providerManifestOsCpuAbi(bad,strlen(bad),revision));
}
