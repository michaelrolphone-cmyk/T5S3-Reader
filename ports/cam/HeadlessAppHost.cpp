#include "HeadlessAppHost.h"
#include "runtime/boot/DefaultAppSelection.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "runtime/packages/PackageUseGate.h"
#include "runtime/resources/ExecutionContext.h"
#include "native/InstalledAppPath.h"
#include "native/AppManifest.h"
#include "native/ManagedAppAdmission.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "native/NativeStreamBridge.h"
#include <NativeAppLauncher.h>
#include <T5AppApi.h>
#include <T5ProviderCapabilityApi.h>
#include <T5StreamApi.h>
#include <Arduino.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <cstring>
#include <string>

namespace {
using RuntimeInstalledProviders::Lease;
RuntimeDevices::AppCapabilityRequirements requirements{};
Lease required[RuntimeDevices::kMaxAppRequirements]{};
size_t requiredCount=0;
struct OptionalLease { Lease lease{}; uint32_t token=0; };
OptionalLease optional[RuntimeDevices::kMaxAppRequirements]{};
uint32_t generation=0;
TaskHandle_t ownerTask=nullptr;
bool active=false;
char lastError[128]{};
void error(const char* value){snprintf(lastError,sizeof(lastError),"%.127s",value?value:"unknown");}
bool owning(){
  auto* context=RuntimeResources::ExecutionContext::current();
  return active && ownerTask==xTaskGetCurrentTaskHandle() && context && context->running(context->id());
}
void releaseRequired(){
  while(requiredCount){
    Lease lease=required[--requiredCount];required[requiredCount]={};
    if(!RuntimeInstalledProviders::release(&lease)) LOG_ERR("APP","required provider did not quiesce");
  }
}
void releaseOptional(){
  for(auto& slot:optional)if(slot.token){
    Lease lease=slot.lease;slot={};
    if(!RuntimeInstalledProviders::release(&lease)) LOG_ERR("APP","provider did not quiesce");
  }
}
bool declared(const char* name,uint32_t version){
  if(!name || !version)return false;
  for(size_t i=0;i<requirements.count;++i)
    if(!strcmp(requirements.entries[i].capability,name) &&
       requirements.entries[i].minApi<=version)return true;
  return false;
}
bool acquire(const char* name,uint32_t version,t5_provider_capability_lease_t* token,const void** interface){
  if(token)*token=0;if(interface)*interface=nullptr;
  if(!owning() || !native_app_current_path() || !token || !interface || !declared(name,version)){
    error("Capability not declared or app not active");return false;
  }
  for(auto& slot:optional)if(!slot.token){
    Lease lease{};
    if(!RuntimeInstalledProviders::acquireCapability(name,version,&lease) || !lease.interface){
      error(RuntimeInstalledProviders::lastError());return false;
    }
    generation=generation>=UINT32_MAX-1?1:generation+1;
    slot.lease=lease;slot.token=generation;*token=generation;*interface=lease.interface;
    return true;
  }
  error("Capability lease table full");return false;
}
bool release(t5_provider_capability_lease_t token){
  if(!owning() || !native_app_current_path() || !token)return false;
  for(auto& slot:optional)if(slot.token==token){
    Lease lease=slot.lease;slot={};
    return RuntimeInstalledProviders::release(&lease);
  }
  return false;
}
bool lastErrorCopy(char* out,size_t capacity){
  if(!owning() || !out || !capacity)return false;
  snprintf(out,capacity,"%s",lastError);return lastError[0]!=0;
}
const t5_provider_capability_api_v1 providerApi={
  T5_PROVIDER_CAPABILITY_API_VERSION,sizeof(t5_provider_capability_api_v1),
  acquire,release,lastErrorCopy};
bool appPoll(t5_app_input_t* input,uint32_t waitMs){
  if(!owning() || !input)return false;
  *input={};
  nativeProviderOwnerTick();
  vTaskDelay(pdMS_TO_TICKS(waitMs>50?50:(waitMs?waitMs:1))+1);
  return owning();
}
uint32_t appMillis(){return owning()?millis():0;}
const t5_app_api_v1* headlessAppApi(){
  static t5_app_api_v1 api{};
  api.abi_version=T5_APP_ABI_VERSION;api.struct_size=sizeof(api);
  api.poll=appPoll;api.millis=appMillis;
  return &api;
}
bool loadRequirements(const char* path){
  requirements={};
  if(!path || strncmp(path,"/sd/",4))return false;
  const std::string elf(path+3);
  if(elf.size()<5 || elf.compare(elf.size()-4,4,".elf"))return false;
  std::shared_ptr<const std::string> captured;
  const auto state=RuntimePackages::captureManagedAppSidecar(path,captured);
  if(state!=RuntimePackages::ManagedAppMetadata::Captured || !captured)return false;
  t5_app_manifest_t manifest{};
  return parseAppManifest(*captured,manifest,nullptr,true,&requirements) &&
    !strcmp(manifest.file_name,elf.c_str()+elf.find_last_of('/')+1) && manifest.compatible;
}
}
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version){
  return version==T5_APP_ABI_VERSION && owning()?headlessAppApi():nullptr;
}
extern "C" const t5_provider_capability_api_v1* t5_provider_capability_get_api(uint32_t version){
  return version==T5_PROVIDER_CAPABILITY_API_VERSION && owning() && native_app_current_path()?&providerApi:nullptr;
}
extern "C" bool native_app_capabilities_ready(const char* path){return owning() && loadRequirements(path);}
extern "C" bool native_app_capabilities_bind(const char* path){
  if(!owning() || !loadRequirements(path) || requiredCount)return false;
  for(size_t i=0;i<requirements.count;++i){
    auto& request=requirements.entries[i];Lease lease{};
    if(!RuntimeInstalledProviders::acquireCapability(request.capability,request.minApi,&lease) ||
       !lease.interface){
      error(RuntimeInstalledProviders::lastError());
      // An uncertain provider start can return a retained grant with no
      // interface. Keep its exact generation for checked cleanup.
      if(lease.grant.slot) required[requiredCount++]=lease;
      releaseRequired();return false;
    }
    required[requiredCount++]=lease;
  }
  return true;
}
extern "C" void native_app_capabilities_release(){releaseRequired();}
extern "C" void native_app_provider_capabilities_release(){releaseOptional();}
extern "C" int native_hardware_compat_register(){return 0;}
extern "C" void native_hardware_compat_unregister(){}
extern "C" void native_hardware_compat_storage_uncertain(){}
extern "C" esp_err_t native_hardware_takeover_begin(uint32_t requested){
  return requested?ESP_ERR_NOT_SUPPORTED:ESP_OK;
}
extern "C" esp_err_t native_hardware_takeover_end(uint32_t requested){
  return requested?ESP_ERR_NOT_SUPPORTED:ESP_OK;
}
extern "C" bool native_hardware_display_is_borrowed(){return false;}
namespace RuntimeBoot {
void runConfiguredDefaultApp(){
  char artifact[96]{};
  const auto choice=RuntimeDefaultApp::read(artifact);
  if(choice==RuntimeDefaultApp::Selection::Absent)return;
  if(choice!=RuntimeDefaultApp::Selection::Ready){LOG_ERR("APP","default selector invalid");return;}
  std::string path;
  if(!resolveInstalledAppPath(artifact,path)){
    LOG_ERR("APP","default unavailable artifact=%s",artifact);return;
  }
  // The normal loader is serialized by its own guard. The host owns one
  // invocation and pins the installed generation until safe unload.
  if(path.compare(0,9,"/sd/Apps/")!=0){
    LOG_ERR("APP","default must be an ordinary managed package");return;
  }
  const auto slash=path.find('/',9);
  if(slash==std::string::npos){LOG_ERR("APP","default package path invalid");return;}
  const std::string root=path.substr(3,slash-3);
  const std::string id=path.substr(9,slash-9);
  constexpr RuntimePackages::PackageRuntimePolicy appPolicy{
      "xtensa-esp32s3",2,8u*1024u*1024u,16u*1024u*1024u};
  RuntimePackages::Identity identity{};
  if(!RuntimePackages::inspectInstalledOrdinarySdDirectory(root.c_str(),appPolicy,
       RuntimePackages::installedCapabilityVersion,identity) ||
     identity.kind!=RuntimePackages::Kind::Application ||
     strcmp(identity.id,id.c_str()) || strcmp(identity.artifact,artifact) ||
     !RuntimePackages::systemPackageUseGate().pin(root.c_str())){
    LOG_ERR("APP","default package admission preflight failed");return;
  }
  ownerTask=xTaskGetCurrentTaskHandle();active=true;
  nativeStreamsBegin();
  const bool ready=RuntimeResources::ExecutionContext::current()!=nullptr;
  const bool admitted=ready && RuntimePackages::beginManagedAppAdmission(identity,path.c_str());
  const esp_err_t result=admitted?launch_elf_app(path.c_str()):ESP_ERR_INVALID_STATE;
  if(admitted)RuntimePackages::endManagedAppAdmission();
  nativeStreamsEnd();active=false;ownerTask=nullptr;
  // A failed loader may retain mapped memory, so retain the package pin.
  if(result==ESP_OK || !admitted)
    (void)RuntimePackages::systemPackageUseGate().unpin(root.c_str());
  LOG_INF("APP","default returned artifact=%s result=%d",artifact,int(result));
}
}
