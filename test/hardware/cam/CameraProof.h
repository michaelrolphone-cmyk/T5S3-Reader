#pragma once
#include <RiscCameraCaptureV1.h>
#include <T5StreamApi.h>
#include <HalStorage.h>
#include <mbedtls/sha256.h>
namespace RuntimeBoot {
/* Explicit lab deployment consumer. No sensor, pins, DMA or model semantics.
 * Normal owner loop continues to poll installed providers between each chunk.
 * Partial output stays .partial; complete output is renamed only after EOF,
 * exact length, close and readback digest verification. Never replaces files. */
inline int cameraProofTick(const risc_camera_capture_api_v1 *api,unsigned cycle) {
  static unsigned previousCycle=0;
  static int result=0;
  static bool done=false,started=false,verifying=false;
  static uint32_t since=0,total=0,checked=0;
  static uint64_t job=0;
  static HalFile file;
  static mbedtls_sha256_context hash;
  static uint8_t digest[32];
  char partial[64],output[64];
  snprintf(partial,sizeof(partial),"/camera-elf-06-%u.jpg.partial",cycle);
  snprintf(output,sizeof(output),"/camera-elf-06-%u.jpg",cycle);
  if(previousCycle!=cycle){
    if(previousCycle && (!done || result!=1))return -1;
    previousCycle=cycle;result=0;done=started=verifying=false;
    since=total=checked=0;job=0;
  }
  if(done)return result;
  auto fail=[&](const char* why){
    if(file)file.close();
    if(started)mbedtls_sha256_free(&hash);
    const int cleanup=job&&api?api->release(api->context,job):0;
    LOG_ERR("CAMERA", "result=failed stage=%s cleanup=%d partial_preserved=1",why,cleanup);
    done=true;result=-1;
  };
  if(!started){
    if(!api || api->api_version!=1 || api->struct_size<sizeof(*api) || !api->capture ||
       !api->read || !api->status || !api->cancel || !api->release){fail("capability");return result;}
    if(Storage.exists(partial)||Storage.exists(output)){fail("output-exists");return result;}
    file=Storage.open(partial,O_WRONLY|O_CREAT|O_EXCL);if(!file){fail("output-open");return result;}
    mbedtls_sha256_init(&hash);mbedtls_sha256_starts_ret(&hash,0);started=true;since=millis();
    risc_camera_request_v1 request{sizeof(request),RISC_CAMERA_JPEG,800,600,12,15000};uint32_t endpoint=0;
    if(api->capture(api->context,&request,&job,&endpoint)!=T5_STREAM_OK){fail("capture");return result;}
    LOG_INF("CAMERA","capture=started stream=%u",endpoint);return result;
  }
  if(millis()-since>60000){fail("deadline");return result;}
  uint8_t bytes[512];uint32_t n=0;
  if(verifying){
    if(checked<total){
      const uint32_t count=std::min(uint32_t(sizeof(bytes)),total-checked);
      if(file.read(bytes,count)!=int(count)){fail("readback");return result;}
      mbedtls_sha256_update_ret(&hash,bytes,count);
      // Private USB lab transport only: never committed/uploaded as an artifact.
      // Receiver checks offsets/length/SHA and decodes locally; no image tuning.
      char hex[1025];const char digits[]="0123456789abcdef";
      for(unsigned i=0;i<count;i++){hex[2*i]=digits[bytes[i]>>4];hex[2*i+1]=digits[bytes[i]&15];}
      hex[2*count]=0;
      Serial.printf("CAMERA_PRIVATE cycle=%u offset=%u hex=%s\n",cycle,checked,hex);
      checked+=count;return result;
    }
    uint8_t actual[32];mbedtls_sha256_finish_ret(&hash,actual);mbedtls_sha256_free(&hash);started=false;
    if(!file.close() || memcmp(actual,digest,32) || !Storage.rename(partial,output)){fail("commit");return result;}
    char text[65];for(unsigned i=0;i<32;i++)snprintf(text+2*i,3,"%02x",digest[i]);
    LOG_INF("CAMERA","result=pass bytes=%u sha256=%s output=%s",total,text,output);done=true;result=1;return result;
  }
  risc_camera_status_v1 state{sizeof(state)};
  if(api->status(api->context,job,&state)!=0){fail("status");return result;}
  if(state.state==RISC_CAMERA_FAILED){
    LOG_ERR("CAMERA","provider_result=%d state=%u bytes=%u sent=%u detail=%.63s",
      state.result,state.state,state.length,state.transferred,state.detail);
    fail("provider");return result;
  }
  const int rc=api->read(api->context,job,bytes,sizeof(bytes),&n);
  if(rc==T5_STREAM_AGAIN)return result;
  if(rc==T5_STREAM_OK){
    if(!n || n>sizeof(bytes) || total+n>96*1024 || file.write(bytes,n)!=n){fail("output-write");return result;}
    mbedtls_sha256_update_ret(&hash,bytes,n);total+=n;return result;
  }
  if(rc!=T5_STREAM_EOF || !total || total!=state.length || state.state!=RISC_CAMERA_DONE){fail("terminal");return result;}
  if(api->release(api->context,job)!=0){fail("release");return result;}job=0;
  if(!file.close()){fail("close");return result;}
  mbedtls_sha256_finish_ret(&hash,digest);mbedtls_sha256_starts_ret(&hash,0);
  file=Storage.open(partial,O_RDONLY);
  if(!file || file.fileSize64()!=total){fail("reopen");return result;}verifying=true;return result;
}
}

namespace RuntimeBoot {
// Full normal graph teardown, not a test graph or direct driver-stop shortcut.
// Cycle 1 revokes an active capture through graph shutdown. Cycle 2 explicitly
// cancels/releases a job, then shuts down; both require zero handles/grants.
template<class Lifecycle,class Adapter>
void cameraLifecycleProofTick(Lifecycle& lifecycle,Adapter& adapter){
  static unsigned cycle=1;static bool stopping=false,done=false;
  static uint32_t since=millis();static uint64_t extraJob=0;
  if(done)return;
  auto fail=[&](const char* reason){LOG_ERR("CAMERA","result=failed stage=%s",reason);done=true;};
  if(millis()-since>180000){fail("lifecycle-deadline");return;}
  if(lifecycle.state()==State::Retained || lifecycle.state()==State::Idle){fail("lifecycle-retained");return;}
  if(stopping){
    if(lifecycle.state()!=State::Cold)return;
    if(BootstrapHalStorage::openHandleCount() || RuntimeInstalledProviders::hasLiveGrants()){
      fail("teardown-resources");return;
    }
    LOG_INF("CAMERA","shutdown=pass cycle=%u handles=0 grants=0",cycle);
    if(cycle==2){done=true;LOG_INF("CAMERA","lifecycle=pass cycles=2 state=Cold");return;}
    cycle=2;stopping=false;extraJob=0;lifecycle.start();
    LOG_INF("CAMERA","restart=1");return;
  }
  if(lifecycle.state()!=State::Running)return;
  const auto* api=static_cast<const risc_camera_capture_api_v1*>(adapter.leases[3].interface);
  const int result=cameraProofTick(api,cycle);
  if(result<0){fail("capture-consumer");return;}
  if(!result)return;
  if(!extraJob){
    risc_camera_request_v1 request{sizeof(request),RISC_CAMERA_JPEG,800,600,12,15000};uint32_t endpoint=0;
    if(api->capture(api->context,&request,&extraJob,&endpoint)!=0){fail("active-teardown-capture");return;}
    LOG_INF("CAMERA","teardown_job=started cycle=%u",cycle);return;
  }
  if(cycle==2){
    int rc=api->cancel(api->context,extraJob);
    if(rc==T5_STREAM_BUSY)return;
    if(rc || api->release(api->context,extraJob)){fail("cancel-release");return;}
    LOG_INF("CAMERA","cancel_release=pass cycle=2");
  }
  stopping=true;lifecycle.stop();
}
}
