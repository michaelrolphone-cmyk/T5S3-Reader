#pragma once
#include "runtime/packages/PackageOrdinarySdZipAdapter.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "HalStorageSdmmcControl.h"
#include "CameraAppPackageIdentity.h"
#include <NativeAppLauncher.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
extern const uint8_t camAppStart[] asm("_binary_dist_release_app_packages_application_camera_utility_0_1_0_xtensa_esp32s3_rte_zip_start");
extern const uint8_t camAppEnd[] asm("_binary_dist_release_app_packages_application_camera_utility_0_1_0_xtensa_esp32s3_rte_zip_end");
namespace RuntimeBoot {
inline bool installCameraAppExperiment(){
  namespace P=RuntimePackages;
  constexpr P::PackageRuntimePolicy policy{"xtensa-esp32s3",2,8u*1024u*1024u,16u*1024u*1024u};
  constexpr const char* id="camera_utility";
  constexpr const char* path="/Inbox/camera-app-1/app.rte.zip";
  const bool storageReady=Storage.begin();
  const bool vfsReady=storageReady && native_app_register_sd_vfs()==ESP_OK;
  if(!vfsReady){LOG_ERR("APP","prepare storage=%u vfs=%u",unsigned(storageReady),unsigned(vfsReady));return false;}
  bool good=true;
  P::OrdinaryTransactionPaths transaction{};
  if(good)good=P::ordinaryTransactionPaths(P::Kind::Application,id,transaction);
  bool installedValid=false;
  if(good && !Storage.exists(transaction.backup) && !Storage.exists(transaction.removing)){
    P::Identity observed{};
    installedValid=P::verifyManagedOrdinarySdDirectory("/Apps/camera_utility",
      P::Kind::Application,id,policy,P::installedCapabilityVersion,observed) &&
      !strcmp(observed.version,"0.1.0") && !observed.legacyVersion;
  }
  if(good && installedValid){
    LOG_INF("APP","reuse id=%s verified=1",id);
  } else if(good){
    LOG_INF("APP","reuse id=%s verified=0",id);
    good=Storage.mkdir("/Inbox/camera-app-1",true);
    const size_t length=camAppEnd-camAppStart;
    good=good && length>0 && length<=64*1024;
    const bool exists=Storage.exists(path);
    auto file=good?Storage.open(path,exists?O_RDONLY:O_WRONLY|O_CREAT|O_EXCL):HalFile{};
    good=good && bool(file) && (!exists || file.fileSize64()==length);
    const uint32_t started=millis();
    for(size_t at=0;good && at<length;){
      if(millis()-started>10000){good=false;break;}
      uint8_t bytes[512];const size_t n=std::min(sizeof(bytes),length-at);
      good=exists?(file.read(bytes,n)==int(n)&&!memcmp(bytes,camAppStart+at,n)):
                  (file.write(camAppStart+at,n)==n);
      at+=n;delay(1);
    }
    if(file)good=file.close()&&good;
    if(!good)LOG_ERR("APP","result=failed stage=inbox-archive");
    if(good){
      P::Identity expected{};
      good=P::makeIdentity(P::Kind::Application,id,"0.1.0","camera_utility.elf",false,&expected);
      if(good){
        const auto outcome=P::installOrdinaryFromSdZip(path,policy,P::installedCapabilityVersion,&expected);
        good=outcome.result==P::OrdinaryInstallResult::Installed;
        LOG_INF("APP","install id=%s result=%u",id,unsigned(outcome.result));
      }
    }
  }
  if(good){
    good=Storage.mkdir("/RiscRTE",true);
    constexpr char value[]="camera_utility.elf\n";
    auto selector=good?Storage.open("/RiscRTE/default-app.txt",
      Storage.exists("/RiscRTE/default-app.txt")?O_RDONLY:O_WRONLY|O_CREAT|O_EXCL):HalFile{};
    good=good&&bool(selector);
    char observed[sizeof(value)]{};
    if(good){
      if(selector.fileSize64()==sizeof(value)-1)
        good=selector.read(observed,sizeof(value)-1)==int(sizeof(value)-1)&&
             !memcmp(observed,value,sizeof(value)-1);
      else if(selector.fileSize64()==0)
        good=selector.write(value,sizeof(value)-1)==sizeof(value)-1;
      else good=false;
    }
    if(selector)good=selector.close()&&good;
  }
  return BootstrapHalStorage::releaseForHandoff() && good;
}
}
