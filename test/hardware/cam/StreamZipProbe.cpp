#include "StreamZipProbe.h"
#include "ZipFixture.h"
#include <Arduino.h>
#include <T5StreamApi.h>
#include <RiscPackageResourcesV1.h>
#include "native/NativeStreamBridge.h"
#include "runtime/drivers/ProviderModuleV2.h"
#include "runtime/packages/PackageIdentity.h"
#include <algorithm>
#include <cstring>
// Trusted diagnostic caller has already verified the exact installed generation;
// the production provider graph holds its package pin throughout this function.
int streamZipProbe(const risc_archive_zip_api_v1* archive) {
  const auto* host=nativeProviderStreamHost();
  RuntimePackages::Identity identity{};
  if(!host || !host->openResources || !RuntimePackages::makeIdentity(
      RuntimePackages::Kind::Service,"archive-zip","0.1.0","driver.elf",false,&identity)) return 50;
  risc_stream_provider_resources_v1 resource{};
  if(!host->openResources(&resource,identity))return 51;
  const auto streams=resource.streams; const uint64_t context=streams.context;
  uint32_t input=0,output=0,readOnly=0,resourceFile=0;
  risc_archive_zip_job job=0;
  const char* failed="none";
  const uint32_t started=millis();
#define NEED(condition,step) do { if(!(condition)){failed=step;return false;} } while(0)
  const bool good=[&]() {
    uint32_t n=0,otherHandle=0; char bytes[64]{};
    NEED(context && resource.open_resource && resource.read_resource,"resource-table");
    NEED(resource.open_resource(context,"../driver.elf",&otherHandle)==T5_STREAM_INVALID && !otherHandle,"resource-traversal-denied");
    NEED(resource.open_resource(context,"driver.elf",&otherHandle)==T5_STREAM_DENIED && !otherHandle,"resource-executable-denied");
    NEED(resource.open_resource(context,"provider-abi.v1",&resourceFile)==T5_STREAM_OK,"resource-open");
    NEED(resource.read_resource(context,resourceFile,bytes,sizeof(bytes),&n)==T5_STREAM_OK && n==40 && !memcmp(bytes,"os-cpu-abi=1\nprovides=archive.zip\napi=1\n",40),"resource-exact-bytes");
    NEED(resource.seek_resource(context,resourceFile,0)==T5_STREAM_OK,"resource-seek");
    NEED(streams.close(context,resourceFile)==T5_STREAM_OK,"resource-close");resourceFile=0;
    Serial.printf("DIAG_STREAM resources=pass context=%llu\n",context);
    risc_stream_endpoint_v1 spec{sizeof(spec),T5_STREAM_BYTES,T5_STREAM_READ|T5_STREAM_WRITE,32,nullptr,0,0};
    NEED(streams.publish(context,&spec,&input)==T5_STREAM_OK,"input-publish");
    NEED(streams.publish(context,&spec,&output)==T5_STREAM_OK,"output-publish");
    spec.rights=T5_STREAM_READ;NEED(streams.publish(context,&spec,&readOnly)==T5_STREAM_OK,"readonly-publish");
    NEED(streams.consume(context,readOnly,bytes,1,&n)==T5_STREAM_DENIED && n==0,"rights-denied");
    risc_stream_provider_v1 outsider{};NEED(host->open(&outsider),"other-context-open");
    const int cross=streams.produce(outsider.context,input,"x",1,&n);host->revoke(outsider.context);host->close(outsider.context);
    NEED(cross==T5_STREAM_INVALID && n==0,"cross-context-denied");
    NEED(archive->begin(sizeof(witnessZip),&job)==RISC_ZIP_OK && job,"cancel-job-begin");
    auto stale=job;NEED(archive->close(job)==RISC_ZIP_OK,"cancel-job-close");job=0;
    NEED(archive->append(stale,witnessZip,1)==RISC_ZIP_STALE,"stale-job-denied");
    NEED(archive->begin(sizeof(witnessZip),&job)==RISC_ZIP_OK && job!=stale,"retry-job-begin");
    // Fill one bounded queue and prove backpressure before consuming its bytes.
    NEED(streams.produce(context,input,witnessZip,32,&n)==T5_STREAM_OK && n==32,"input-fill");
    NEED(streams.produce(context,input,"x",1,&n)==T5_STREAM_AGAIN && n==0,"input-backpressure");
    NEED(streams.consume(context,input,bytes,32,&n)==T5_STREAM_OK && n==32,"input-first-read");
    NEED(archive->append(job,bytes,n)==RISC_ZIP_OK,"zip-first-append");
    for(size_t at=32;at<sizeof(witnessZip);){
      NEED(millis()-started<5000,"io-deadline");
      const uint32_t count=std::min(size_t(16),sizeof(witnessZip)-at);
      NEED(streams.produce(context,input,witnessZip+at,count,&n)==T5_STREAM_OK && n==count,"input-produce");
      NEED(streams.consume(context,input,bytes,count,&n)==T5_STREAM_OK && n==count,"input-consume");
      NEED(archive->append(job,bytes,n)==RISC_ZIP_OK,"zip-append");at+=n;delay(1);
    }
    NEED(streams.finish(context,input,T5_STREAM_EOF)==T5_STREAM_OK,"input-finish");
    NEED(archive->seal(job)==RISC_ZIP_OK,"zip-seal");uint32_t count=0;risc_archive_zip_entry_v1 entry{};
    NEED(archive->count(job,&count)==RISC_ZIP_OK && count==1,"zip-count");
    NEED(archive->entry(job,0,&entry)==RISC_ZIP_OK && !strcmp(entry.name,"proof.txt") && entry.size_bytes==8,"zip-entry");
    NEED(archive->read(job,0,0,bytes,8,&n)==RISC_ZIP_OK && n==8,"zip-read");
    NEED(streams.produce(context,output,bytes,n,&count)==T5_STREAM_OK && count==8,"output-produce");memset(bytes,0,sizeof(bytes));
    NEED(streams.consume(context,output,bytes,8,&n)==T5_STREAM_OK && n==8 && !memcmp(bytes,"RiscRTE\n",8),"output-exact-bytes");
    NEED(streams.finish(context,output,T5_STREAM_EOF)==T5_STREAM_OK,"output-finish");
    NEED(archive->close(job)==RISC_ZIP_OK,"zip-close");job=0;
    Serial.printf("DIAG_PROVIDER id=archive-zip input_stream_bytes=%u extracted_bytes=8 exact_match=1 backpressure=pass rights=pass retry=pass\n",unsigned(sizeof(witnessZip)));
    return true;
  }();
#undef NEED
  bool closed=true;
  if(job && archive->close(job)!=RISC_ZIP_OK)closed=false;
  for(auto handle:{input,output,readOnly,resourceFile}) if(handle && streams.close(context,handle)!=T5_STREAM_OK)closed=false;
  host->revoke(context); uint32_t n=99;
  const bool revoked=streams.produce(context,input,"x",1,&n)==T5_STREAM_DENIED && n==0;
  host->close(context);
  risc_stream_provider_v1 next{};bool generation=host->open(&next) && next.context && next.context!=context;
  if(next.context){host->revoke(next.context);host->close(next.context);}
  Serial.printf("DIAG_STREAM cleanup=%u revoked=%u fresh_generation=%u failed=%s\n",unsigned(closed),unsigned(revoked),unsigned(generation),failed);
  if(!closed)return 35;
  return good&&revoked&&generation?0:52;
}
