#pragma once
#include "runtime/packages/PackageOrdinarySdZipAdapter.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "HalStorageSdmmcControl.h"
/* Embedding is a lab transport for ordinary .rte.zip bytes, not a firmware
 * camera implementation or second installer. Only explicit camera experiment
 * environment includes this. Existing installed generations must match exactly
 * through the ordinary manager's version/integrity/recovery rules. */
extern const uint8_t camProfileStart[] asm("_binary_dist_packages_driver_cam_ov3660_profile_0_1_0_xtensa_esp32s3_rte_zip_start");
extern const uint8_t camProfileEnd[] asm("_binary_dist_packages_driver_cam_ov3660_profile_0_1_0_xtensa_esp32s3_rte_zip_end");
extern const uint8_t camDriverStart[] asm("_binary_dist_packages_driver_camera_esp32s3_ov3660_0_1_1_xtensa_esp32s3_rte_zip_start");
extern const uint8_t camDriverEnd[] asm("_binary_dist_packages_driver_camera_esp32s3_ov3660_0_1_1_xtensa_esp32s3_rte_zip_end");
namespace RuntimeBoot {
inline bool installCameraExperiment() {
  namespace P=RuntimePackages;
  struct Item{const char* id;const char* version;const char* path;const uint8_t* begin;const uint8_t* end;};
  const Item items[]{
    {"cam-ov3660-profile","0.1.0","/Inbox/camera-elf-02/profile.rte.zip",camProfileStart,camProfileEnd},
    {"camera-esp32s3-ov3660","0.1.1","/Inbox/camera-elf-02/camera.rte.zip",camDriverStart,camDriverEnd}
  };
  if(!Storage.begin() || native_app_register_sd_vfs()!=ESP_OK)return false;
  bool good=Storage.mkdir("/Inbox/camera-elf-02",true);
  const uint32_t began=millis();
  for(const auto& item:items){
    if(!good)break;const size_t length=item.end-item.begin;
    if(!length || length>128*1024){good=false;break;}
    bool exists=Storage.exists(item.path);auto f=Storage.open(item.path,exists?O_RDONLY:O_WRONLY|O_CREAT|O_EXCL);
    good=bool(f) && (!exists || f.fileSize64()==length);
    for(size_t at=0;good && at<length;){
      if(millis()-began>30000){good=false;break;}
      uint8_t bytes[512];size_t n=std::min(sizeof(bytes),length-at);
      good=exists?(f.read(bytes,n)==int(n)&&!memcmp(bytes,item.begin+at,n)):(f.write(item.begin+at,n)==n);
      at+=n;delay(1);
    }
    good=f.close()&&good;if(!good)break;
    P::Identity expected{};
    good=P::makeIdentity(P::Kind::Driver,item.id,item.version,"driver.elf",false,&expected);
    if(!good)break;
    constexpr P::PackageRuntimePolicy policy{"xtensa-esp32s3",2,8u*1024u*1024u,16u*1024u*1024u};
    auto result=P::installOrdinaryFromSdZip(item.path,policy,P::installedCapabilityVersion,&expected);
    LOG_INF("CAMERA","install id=%s result=%u",item.id,unsigned(result.result));
    good=result.result==P::OrdinaryInstallResult::Installed;
  }
  return BootstrapHalStorage::releaseForHandoff() && good;
}
}
