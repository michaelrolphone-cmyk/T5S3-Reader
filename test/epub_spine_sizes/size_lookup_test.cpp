#include "HalStorage.h"
#include "BookMetadataCache.h"
#include "ZipFile.h"
#include "Serialization.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <new>
Counts counts;
uint32_t clockMs=0, readMs=0;
std::string archivePath, fault;
int liveArchives=0, liveTemps=0, failAllocation=0;
int failReadCountdown=0, readFailure=0, failSeekCountdown=0;
size_t allocationBytes=0;
StorageFixture Storage;
uint32_t millis(){return clockMs;}
void vTaskDelay(int){++counts.yields;++clockMs;}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
 allocationBytes=std::max(allocationBytes,size);
 if(failAllocation>0&&--failAllocation==0)return nullptr;
 return ::operator new[](size);
}
namespace FsHelpers {
#include "normalise.inc"
}
static std::string name(int i){char n[80];std::snprintf(n,sizeof(n),"OPS/ch%04d.xhtml",i);return n;}
static std::shared_ptr<MemFile> load(const char* path){
 auto f=std::make_shared<MemFile>();std::ifstream in(path,std::ios::binary);assert(in);
 f->bytes=std::vector<uint8_t>(std::istreambuf_iterator<char>(in),{});return f;
}
static void put32(size_t p,uint32_t v){for(int i=0;i<4;++i)Storage.files[archivePath]->bytes[p+i]=uint8_t(v>>(8*i));}
static void seed(BookMetadataCache& cache,int n,const std::string& mode){
 assert(cache.beginWrite()&&cache.beginContentOpfPass());
 for(int i=0;i<n;++i)cache.createSpineEntry(mode=="normalize"?"OPS/tmp/../"+name(i).substr(4):name(i));
 assert(cache.endContentOpfPass()&&cache.beginTocPass());
 cache.createTocEntry("Start",mode=="normalize"?"OPS/tmp/../"+name(0).substr(4):name(0),"",0);
 assert(cache.endTocPass()&&cache.endWrite());
}
int main(int argc,char**argv){
 assert(argc==5);archivePath=argv[1];int n=std::stoi(argv[2]);std::string mode=argv[3];
 Storage.files[archivePath]=load(argv[1]);
 if(mode=="direct"){
#ifndef BASELINE_SOURCE
  // The production private offsets are inspected with compiler test-only
  // access control disabled. No alternate implementation is compiled.
  ZipFile zip(archivePath);assert(!zip.prepareSizeLookupOffsets());assert(zip.open());
  assert(zip.prepareSizeLookupOffsets());assert(zip.sizeLookupCount==n);
  assert(allocationBytes<=16384);assert(!zip.prepareSizeLookupOffsets());
  size_t sz=0;
  assert(zip.getInflatedFileSize("dup",&sz)&&sz==11);
  assert(zip.getInflatedFileSize("unique",&sz)&&sz==23);
  assert(zip.getInflatedFileSize("dup",&sz)&&sz==37); // cursor-sensitive duplicate, not first-only
  assert(zip.getInflatedFileSize("dup",&sz)&&sz==11);
  assert(zip.getInflatedFileSize("zero",&sz)&&sz==0);
  assert(zip.getInflatedFileSize("nul",&sz)&&sz==7); // embedded-NUL central name preserves strcmp
  assert(!zip.getInflatedFileSize("missing",&sz));
  assert(zip.getInflatedFileSize("unique",&sz)&&sz==23);
  // Artificial equal-hash bucket must still execute exact-name ordinary lookup.
  uint16_t u=0;for(uint16_t i=0;i<zip.sizeLookupCount;++i)if(zip.sizeLookupOffsets[i].hash==ZipFile::fnvHash64("unique",6))u=i;
  const auto saved=zip.sizeLookupOffsets[0].hash;zip.sizeLookupOffsets[0].hash=zip.sizeLookupOffsets[u].hash;
  assert(zip.getInflatedFileSize("unique",&sz)&&sz==23);zip.sizeLookupOffsets[0].hash=saved;
  // A single hash-only false candidate cannot be used as identity proof.
  zip.sizeLookupOffsets[u].hash=ZipFile::fnvHash64("absent",6);
  assert(!zip.getInflatedFileSize("absent",&sz));assert(zip.sizeLookupCount==0);
  assert(zip.getInflatedFileSize("unique",&sz)&&sz==23);
  assert(zip.prepareSizeLookupOffsets());
  const uint32_t uoff=zip.sizeLookupOffsets[u].offset;
  put32(uoff+24,29); // size is reread; it is never cached in the hint table
  assert(zip.getInflatedFileSize("unique",&sz)&&sz==29);put32(uoff+24,23);
  for(int f : {-1,0,3}){
   zip.close();assert(zip.open()&&zip.prepareSizeLookupOffsets());
   failReadCountdown=1;readFailure=f;assert(zip.getInflatedFileSize("unique",&sz)&&sz==23);
   assert(zip.sizeLookupCount==0);assert(zip.getInflatedFileSize("dup",&sz)&&sz==37);
  }
  for(int at : {1,2,3}){
   zip.close();assert(zip.open()&&zip.prepareSizeLookupOffsets());
   failReadCountdown=at;readFailure=0;
   // Only the first two reads belong to the hint. Third-read fault isn't consumed here.
   assert(zip.getInflatedFileSize("unique",&sz)&&sz==23);failReadCountdown=0;
  }
  for(int at : {1,2}){
   zip.close();assert(zip.open()&&zip.prepareSizeLookupOffsets());failSeekCountdown=at;
   assert(zip.getInflatedFileSize("unique",&sz)&&sz==23);assert(zip.sizeLookupCount==0);
  }
  zip.close();assert(zip.sizeLookupCount==0&&!zip.sizeLookupOffsets&&liveArchives==0);
  assert(zip.open()&&zip.prepareSizeLookupOffsets());
  // Reopening, even without an explicit close call, cannot retain hints.
  assert(zip.open()&&zip.sizeLookupCount==0&&!zip.sizeLookupOffsets);zip.close();
  for(int at : {1,2,3,5}){
   ZipFile retry(archivePath);assert(retry.open()&&retry.loadZipDetails());failReadCountdown=at;readFailure=-1;
   assert(!retry.prepareSizeLookupOffsets());failReadCountdown=0;
   assert(retry.getInflatedFileSize("unique",&sz)&&sz==23);retry.close();
  }
  // Prefix corruption makes the table ineligible, retaining baseline lookup failure.
  auto original=Storage.files[archivePath]->bytes;
  ZipFile bad(archivePath);assert(bad.open()&&bad.loadZipDetails());
  put32(bad.zipDetails.centralDirOffset,0);assert(!bad.prepareSizeLookupOffsets());
  assert(!bad.getInflatedFileSize("unique",&sz));bad.close();Storage.files[archivePath]->bytes=original;
  std::puts("{\"direct_cases\":24}");
#endif
  return 0;
 }
 BookMetadataCache cache("/cache");seed(cache,n,mode);
 BookMetadataCache::BookMetadata md;md.title="Size probe";md.author="Author";md.language="en";
 counts={};
 if(mode=="allocation")failAllocation=1;
 if(mode=="metadata"||mode=="seek"||mode=="open")fault=mode;
 if(mode=="slow")readMs=2;
 if(mode=="timeout")readMs=1000;
 if(mode=="rollover"){clockMs=UINT32_MAX-10;readMs=2;}
 const bool okay=cache.buildBookBin(archivePath,md);assert(okay==(mode!="open"));
 Counts cold=counts;assert(cold.opens==cold.closes&&liveArchives==0&&liveTemps==0);assert(allocationBytes<=16384);
 if(!okay){fault.clear();assert(cache.buildBookBin(archivePath,md));}
 auto bytes=Storage.files.at("/cache/book.bin")->bytes;
 std::ofstream out(argv[4],std::ios::binary);out.write((const char*)bytes.data(),bytes.size());out.close();
 FsFile result;assert(Storage.openFileForRead("check","/cache/book.bin",result));
 uint8_t version=0;uint32_t lut=0;uint16_t spines=0,toc=0;
 serialization::readPod(result,version);serialization::readPod(result,lut);serialization::readPod(result,spines);serialization::readPod(result,toc);assert(spines==n&&toc==1);
 size_t cumulative=0;
 for(int i=0;i<n;++i){
  assert(result.seek(lut+4*i));uint32_t off=0;serialization::readPod(result,off);assert(result.seek(off));
  std::string href;size_t total=0;int16_t ti=-1;serialization::readString(result,href);serialization::readPod(result,total);serialization::readPod(result,ti);
  cumulative+=256+size_t(i);assert(total==cumulative&&ti==0);
  assert(href==(mode=="normalize"?"OPS/tmp/../"+name(i).substr(4):name(i)));
 }
 result.close();
 // Normal/error retry builds identically; temporary files close before removal.
 readMs=0;fault.clear();failAllocation=0;assert(cache.buildBookBin(archivePath,md));
 assert(Storage.files.at("/cache/book.bin")->bytes==bytes);assert(cache.cleanupTmpFiles());
 assert(Storage.files.size()==2&&liveArchives==0&&liveTemps==0);
 std::printf("{\"spine\":%d,\"entries\":%llu,\"reads\":%llu,\"bytes\":%llu,\"seeks\":%llu,\"opens\":%llu,\"closes\":%llu,\"yields\":%llu,\"allocation_bytes\":%zu}\n",n,(unsigned long long)cold.entries,(unsigned long long)cold.reads,(unsigned long long)cold.bytes,(unsigned long long)cold.seeks,(unsigned long long)cold.opens,(unsigned long long)cold.closes,(unsigned long long)cold.yields,allocationBytes);
}
