#include <HalStorage.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <freertos/task.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

TestStorage Storage;
static unsigned allocations=0,liveAllocations=0,failAllocation=0;
static size_t largestAllocation=0;
void* rowAllocate(size_t n){++allocations;largestAllocation=std::max(largestAllocation,n);if(allocations==failAllocation)return nullptr;void*p=std::malloc(n);if(p)++liveAllocations;return p;}
void rowFree(void*p){if(p){assert(liveAllocations);--liveAllocations;}std::free(p);}
static uint64_t hash(const void*ptr,size_t n){auto*p=static_cast<const uint8_t*>(ptr);uint64_t h=1469598103934665603ull;while(n--){h^=*p++;h*=1099511628211ull;}return h;}
static void put16(std::vector<uint8_t>&v,size_t p,uint16_t x){v[p]=x;v[p+1]=x>>8;}
static void put32(std::vector<uint8_t>&v,size_t p,uint32_t x){for(unsigned i=0;i<4;++i)v[p+i]=x>>(8*i);}
static std::vector<uint8_t> bmp(int w,int h,unsigned bpp,bool top,bool highColor=false){
 const size_t palette=bpp<=8?(highColor?(1u<<bpp):std::min(1u<<bpp,4u)):0,offset=54+palette*4,row=((w*bpp+31)/32)*4;
 std::vector<uint8_t> bytes(offset+row*h);bytes[0]='B';bytes[1]='M';put32(bytes,2,bytes.size());put32(bytes,10,offset);put32(bytes,14,40);
 put32(bytes,18,w);put32(bytes,22,top?uint32_t(-h):uint32_t(h));put16(bytes,26,1);put16(bytes,28,bpp);put32(bytes,46,palette);
 for(size_t p=0;p<palette;++p){uint8_t level=highColor?uint8_t(p*13+17):uint8_t((p%4)*85);for(unsigned c=0;c<3;++c)bytes[54+p*4+c]=level;}
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){unsigned val=((x/7+y/9)%4);size_t p=offset+row*y;
  if(bpp<=8){unsigned idx=val%palette;bytes[p+x*bpp/8]|=idx<<(8-bpp-(x*bpp%8));}
  else for(unsigned c=0;c<bpp/8;++c)bytes[p+x*(bpp/8)+c]=uint8_t((x*17+y*5+c*41)&255);
 }
 return bytes;
}
static void reset(){assert(Storage.openFiles==0&&liveAllocations==0);Storage.files.clear();trace.clear();
 readCalls=readBytes=readLocks=requestedDelays=rowYields=clockMs=lastYieldClock=maxYieldGap=0;
 allocations=liveAllocations=failAllocation=0;largestAllocation=0;clockStep=providerMillis=pixelMillisEvery=0;
 slowOffset=0;errorOffset=maxProviderRead=mediaLossOffset=SIZE_MAX;providerError=false;ready=true;failLock=failSeek=false;
}
static void run(int w,int h,unsigned bpp,bool top,unsigned passes=1,int fault=0,int geometry=0,bool dither=false){
 reset();Storage.files["/image.bmp"]=bmp(w,h,bpp,top,dither);FsFile file;assert(Storage.openFileForRead("BMP","/image.bmp",file));
 Bitmap bitmap(file,dither);const auto parsed=bitmap.parseHeaders();assert(parsed==BmpReaderError::Ok);
 const size_t offset=file.position,row=bitmap.getRowBytes();
 const auto headerReads=readCalls,headerBytes=readBytes,headerWaits=requestedDelays;
 GfxRenderer gfx;gfx.width=std::max(w,540);gfx.height=std::max(h,960);FontCacheManager fonts;gfx.fontCacheManager_=&fonts;
 int x=0,y=0,mw=0,mh=0;float cropX=0,cropY=0;
 if(geometry==1){x=-23;y=-41;}
 if(geometry==2){mw=w/2;mh=h/2;x=13;y=11;}
 if(geometry==3){cropX=0.25f;cropY=0.125f;}
 if(geometry==4){y=gfx.height+10;}
 if(geometry==5){fonts.scanning=true;}
 if(fault==1)Storage.files["/image.bmp"].resize(offset+row*17+1);
 if(fault==2)errorOffset=offset+row*17;
 if(fault==3)maxProviderRead=7;
 if(fault==4)ready=false;
 if(fault==5)failLock=true;
 if(fault==6||fault==7)failAllocation=fault-5;
 if(fault==8)mediaLossOffset=offset+row*17;
 if(fault==9)providerMillis=3;
 if(fault==10)providerMillis=9;
 if(fault==11){providerMillis=5000;maxProviderRead=7;slowOffset=offset;}
 if(fault==12)pixelMillisEvery=64;
 if(fault==13)clockMs=UINT32_MAX-4ull,lastYieldClock=clockMs,providerMillis=3;
 const auto unchangedInput=Storage.files["/image.bmp"];
 uint64_t frames=1469598103934665603ull;
 for(unsigned pass=0;pass<passes;++pass){
  if(pass){providerError=false;ready=true;failLock=false;assert(bitmap.rewindToData()==BmpReaderError::Ok);}
  gfx.renderMode=static_cast<GfxRenderer::RenderMode>(pass%3);gfx.clear();
  gfx.drawBitmap(bitmap,x,y,mw,mh,cropX,cropY);
  const auto frame=hash(gfx.pixels.data(),gfx.pixels.size());frames=(frames^frame)*1099511628211ull;
 }
 const auto pixelReads=readCalls-headerReads,pixelBytes=readBytes-headerBytes,pixelWaits=requestedDelays-headerWaits+rowYields;
 if(!fault&&!geometry){assert(pixelBytes==passes*row*size_t(h));assert(pixelReads==passes*size_t(h)*((row+4095)/4096));
#ifdef TEST_BASELINE
  assert(pixelWaits==pixelReads);
#else
  if(row<4096){const unsigned interval=std::min<size_t>(31,(4096+row-1)/row);
   assert(pixelWaits<=passes*(1+(h+interval-1)/interval));assert(pixelWaits<pixelReads);
  }else assert(pixelWaits<=pixelReads);
#endif
 }
 if(fault==9||fault==10||fault==13)assert(maxYieldGap<=8+providerMillis);
 if(fault==12)assert(maxYieldGap<=8+static_cast<unsigned>(w+63)/64);
 assert(largestAllocation<=std::max(row,size_t((w+3)/4))&&liveAllocations==0);
 assert(Storage.files["/image.bmp"]==unchangedInput);
 const auto position=file.position;const auto sticky=file.impl->error;
 printf("case %dx%d/%u top=%d passes=%u fault=%d geom=%d dither=%d frame=%016llx trace=%016llx reads=%llu bytes=%llu position=%zu error=%u alloc=%u max=%zu\n",
 w,h,bpp,top,passes,fault,geometry,dither,(unsigned long long)frames,(unsigned long long)hash(trace.data(),trace.size()*sizeof(uint64_t)),
 (unsigned long long)readCalls,(unsigned long long)readBytes,position,sticky,allocations,largestAllocation);
 fprintf(stderr,"cost %dx%d/%u top=%d passes=%u fault=%d geom=%d rows=%llu header_waits=%llu pixel_waits=%llu gap=%llu\n",w,h,bpp,top,passes,fault,geometry,(unsigned long long)pixelReads,(unsigned long long)headerWaits,(unsigned long long)pixelWaits,(unsigned long long)maxYieldGap);
 file.close();assert(!Storage.openFiles&&Storage.handles.empty());
}
static void directRetry(){
 reset();Storage.files["/image.bmp"]=bmp(480,800,4,false);FsFile file;assert(Storage.openFileForRead("BMP","/image.bmp",file));Bitmap image(file);assert(image.parseHeaders()==BmpReaderError::Ok);
 std::vector<uint8_t> packed(120),row(image.getRowBytes());const auto offset=file.position;
 failLock=true;assert(image.readNextRow(packed.data(),row.data())==BmpReaderError::ShortReadRow);assert(file.position==offset);
 failLock=false;assert(image.readNextRow(packed.data(),row.data())==BmpReaderError::Ok);
 auto first=packed;assert(image.rewindToData()==BmpReaderError::Ok);assert(image.readNextRow(packed.data(),row.data())==BmpReaderError::Ok);assert(first==packed);
 failSeek=true;assert(image.rewindToData()==BmpReaderError::SeekPixelDataFailed);failSeek=false;assert(image.rewindToData()==BmpReaderError::Ok);
 file.close();assert(!Storage.openFiles);puts("direct default caller read/retry/rewind PASS");
}
static void cooperativeRetry(){
 reset();Storage.files["/image.bmp"]=bmp(480,800,4,false);FsFile file;assert(Storage.openFileForRead("BMP","/image.bmp",file));Bitmap image(file);assert(image.parseHeaders()==BmpReaderError::Ok);
 std::vector<uint8_t> packed(120),row(image.getRowBytes());const auto offset=file.position;
#ifndef TEST_BASELINE
 HalReadBudget budget([]()->uint32_t{return millis();},[](){vTaskDelay(1);});
 auto readRow=[&](){return image.readNextRow(packed.data(),row.data(),&budget);};
#else
 auto readRow=[&](){return image.readNextRow(packed.data(),row.data());};
#endif
 const auto before=readCalls;failLock=true;clockMs+=9;
 assert(readRow()==BmpReaderError::ShortReadRow&&file.position==offset&&readCalls==before);
#ifndef TEST_BASELINE
 assert(rowYields>0); // Elapsed-time cooperation also covers zero-progress failure.
#endif
 failLock=false;assert(readRow()==BmpReaderError::Ok);const auto first=packed;
 assert(image.rewindToData()==BmpReaderError::Ok);assert(readRow()==BmpReaderError::Ok&&packed==first);
 file.close();assert(!Storage.openFiles);puts("opt-in failed-read retry and zero-progress cooperation PASS");
}
int main(){
 for(auto d:{std::pair<int,int>{240,400},{480,800},{540,960}})for(unsigned bpp:{1u,2u,4u,8u,24u,32u})for(bool top:{false,true})run(d.first,d.second,bpp,top,bpp==1?1:3);
 for(int fault=1;fault<=13;++fault)for(bool top:{false,true})run(480,800,4,top,1,fault);
 for(int geometry=1;geometry<=5;++geometry)for(unsigned bpp:{1u,4u,24u})for(bool top:{false,true})run(137,99,bpp,top,1,0,geometry);
 for(unsigned bpp:{4u,8u,24u,32u})for(bool top:{false,true})run(73,63,bpp,top,3,0,0,true);
 // Maximum permitted row, packed tail and provider chunk boundaries.
 for(unsigned bpp:{1u,4u,24u,32u})run(2048,33,bpp,true);
 run(2048,33,32,true,1,11);
 for(bool top:{false,true})run(2048,33,4,top,1,12);
 run(1,1,1,true);directRetry();cooperativeRetry();puts("BMP production read/render behavior, costs and cleanup PASS");
}
