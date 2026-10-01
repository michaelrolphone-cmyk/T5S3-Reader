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
inline void cameraProofTick(const risc_camera_capture_api_v1 *api) {
  static bool done=false,started=false,verifying=false;
  static uint32_t since=0,total=0,checked=0;
  static uint64_t job=0;
  static HalFile file;
  static mbedtls_sha256_context hash;
  static uint8_t digest[32];
  constexpr auto partial="/camera-elf-03.jpg.partial";
  constexpr auto output="/camera-elf-03.jpg";
  if(done)return;
  auto fail=[&](const char* why){
    if(file)file.close();
    if(started)mbedtls_sha256_free(&hash);
    const int cleanup=job&&api?api->release(api->context,job):0;
    LOG_ERR("CAMERA", "result=failed stage=%s cleanup=%d partial_preserved=1",why,cleanup);
    done=true;
  };
  if(!started){
    if(!api || api->api_version!=1 || api->struct_size<sizeof(*api) || !api->capture ||
       !api->read || !api->status || !api->cancel || !api->release){fail("capability");return;}
    if(Storage.exists(partial)||Storage.exists(output)){fail("output-exists");return;}
    file=Storage.open(partial,O_WRONLY|O_CREAT|O_EXCL);if(!file){fail("output-open");return;}
    mbedtls_sha256_init(&hash);mbedtls_sha256_starts_ret(&hash,0);started=true;since=millis();
    risc_camera_request_v1 request{sizeof(request),RISC_CAMERA_JPEG,800,600,12,15000};uint32_t endpoint=0;
    if(api->capture(api->context,&request,&job,&endpoint)!=T5_STREAM_OK){fail("capture");return;}
    LOG_INF("CAMERA","capture=started stream=%u",endpoint);return;
  }
  if(millis()-since>25000){fail("deadline");return;}
  uint8_t bytes[512];uint32_t n=0;
  if(verifying){
    if(checked<total){
      const uint32_t count=std::min(uint32_t(sizeof(bytes)),total-checked);
      if(file.read(bytes,count)!=int(count)){fail("readback");return;}
      mbedtls_sha256_update_ret(&hash,bytes,count);checked+=count;return;
    }
    uint8_t actual[32];mbedtls_sha256_finish_ret(&hash,actual);mbedtls_sha256_free(&hash);started=false;
    if(!file.close() || memcmp(actual,digest,32) || !Storage.rename(partial,output)){fail("commit");return;}
    char text[65];for(unsigned i=0;i<32;i++)snprintf(text+2*i,3,"%02x",digest[i]);
    LOG_INF("CAMERA","result=pass bytes=%u sha256=%s output=%s",total,text,output);done=true;return;
  }
  risc_camera_status_v1 state{sizeof(state)};
  if(api->status(api->context,job,&state)!=0){fail("status");return;}
  if(state.state==RISC_CAMERA_FAILED){
    LOG_ERR("CAMERA","provider_result=%d state=%u bytes=%u sent=%u detail=%.63s",
      state.result,state.state,state.length,state.transferred,state.detail);
    fail("provider");return;
  }
  const int rc=api->read(api->context,job,bytes,sizeof(bytes),&n);
  if(rc==T5_STREAM_AGAIN)return;
  if(rc==T5_STREAM_OK){
    if(!n || n>sizeof(bytes) || total+n>96*1024 || file.write(bytes,n)!=n){fail("output-write");return;}
    mbedtls_sha256_update_ret(&hash,bytes,n);total+=n;return;
  }
  if(rc!=T5_STREAM_EOF || !total || total!=state.length || state.state!=RISC_CAMERA_DONE){fail("terminal");return;}
  if(api->release(api->context,job)!=0){fail("release");return;}job=0;
  if(!file.close()){fail("close");return;}
  mbedtls_sha256_finish_ret(&hash,digest);mbedtls_sha256_starts_ret(&hash,0);
  file=Storage.open(partial,O_RDONLY);
  if(!file || file.fileSize64()!=total){fail("reopen");return;}verifying=true;
}
}
