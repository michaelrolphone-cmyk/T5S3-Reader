#include "HalStorage.h"
#include "Arduino.h"
#include "ZipFile.h"
#include "Logging.h"
#include "CssParser.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <new>
Counts counts;
uint32_t clockMs=0, readMs=0;
std::string archivePath, fault;
int liveArchives=0,liveTemps=0;
uint32_t heap=1024*1024;
StorageFixture Storage;
EspFixture ESP;
uint32_t millis(){return clockMs;}
void vTaskDelay(int){++counts.yields;++clockMs;}
int failAllocation=0;
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
 if(failAllocation>0&&--failAllocation==0)return nullptr;
 return ::operator new[](size);
}
bool nearHeap=false;
uint32_t EspFixture::getFreeHeap() const {return nearHeap&&liveArchives?60000:heap;}
namespace FsHelpers {
#include "normalise.inc"
}
struct ObservedCssParser : CssParser {
 std::vector<std::string> contents;
 ObservedCssParser():CssParser("/cache"){}
 bool loadFromStream(FsFile& file){
   std::string s;uint8_t b[1024];while(file.available()){int n=file.read(b,sizeof(b));assert(n>0);s.append((const char*)b,n);}contents.push_back(s);
   assert(file.seek(0));return CssParser::loadFromStream(file);
 }
};
class Epub {
 public:
 std::string filepath;
 std::vector<std::string> cssFiles;
 std::unique_ptr<ObservedCssParser> cssParser=std::make_unique<ObservedCssParser>();
 const std::string getCachePath()const{return "/cache";}
 void parseCssFiles()const;
 bool readItemContentsToStream(const std::string&,Print&,size_t)const;
 bool getItemSize(const std::string&,size_t*)const;
};
#include "epub_functions.inc"
static std::shared_ptr<MemFile> load(const char* path){
 auto f=std::make_shared<MemFile>();std::ifstream in(path,std::ios::binary);assert(in);
 f->bytes=std::vector<uint8_t>(std::istreambuf_iterator<char>(in),{});return f;
}
int main(int argc,char**argv){
 assert(argc==4);archivePath=argv[1];int cssCount=std::stoi(argv[2]);std::string mode=argv[3];
 Storage.files[archivePath]=load(argv[1]);
 Epub epub;epub.filepath=archivePath;
 for(int i=0;i<cssCount;++i)epub.cssFiles.push_back("OEBPS/style"+std::to_string(i)+".css");
 if(mode=="reverse")std::reverse(epub.cssFiles.begin(),epub.cssFiles.end());
 if(mode=="duplicates")epub.cssFiles.push_back(epub.cssFiles.front());
 if(mode=="missing")epub.cssFiles.push_back("OEBPS/missing.css");
 if(mode=="normalize")for(auto& p:epub.cssFiles)p="OEBPS/unused/../"+p.substr(6);
 if(mode=="lowheap")heap=65535;
 if(mode=="threshold")heap=65536;
 if(mode=="release-heap"){heap=81920;nearHeap=true;}
 if(mode=="alloc1")failAllocation=1;
 if(mode=="alloc2")failAllocation=2;
 if(mode=="slow")readMs=2;
 if(mode=="timeout")readMs=1000;
 if(mode=="rollover"){clockMs=UINT32_MAX-10;readMs=2;}
 if(mode=="metadata"||mode=="seek"||mode=="open"||mode=="header"||mode=="write"||mode=="temp-read"||mode=="temp-write")fault=mode;
 epub.parseCssFiles();
 assert(liveArchives==0&&liveTemps==0);assert(!Storage.exists("/cache/.tmp.css"));assert(counts.opens==counts.closes);
 size_t expected=cssCount+(mode=="duplicates"?1:0);
 if(mode=="lowheap")expected=0;
 if(mode=="header"||mode=="write"||mode=="temp-read"||mode=="temp-write")--expected;
 assert(epub.cssParser->contents.size()==expected);
 for(size_t i=0;i<expected;++i){
   size_t index=i;
   if(mode=="reverse")index=cssCount-1-i;
   if(mode=="duplicates"&&i==size_t(cssCount))index=0;
   if(mode=="header"||mode=="write"||mode=="temp-read"||mode=="temp-write")++index;
   assert(epub.cssParser->contents[i]=="p { margin-left: "+std::to_string(index)+"px; }\n");
 }
 Counts cold=counts;
 std::string cacheHex;
 for(const auto& f:Storage.files)if(f.first!=archivePath){for(uint8_t b:f.second->bytes){static const char hex[]="0123456789abcdef";cacheHex+=hex[b>>4];cacheHex+=hex[b&15];}}
 epub.parseCssFiles();assert(counts.reads==cold.reads);
 CssParser restored("/cache");assert(restored.loadFromCache());
 if(expected){auto style=restored.resolveStyle("p","");assert(style.defined.marginLeft);float last=mode=="reverse"||mode=="duplicates"?0:float(cssCount-1);assert(style.marginLeft.value==last);}
 else assert(restored.empty());
 // Rebuild/retry after clearing the cache must not retain the prior archive or fault.
 for(auto it=Storage.files.begin();it!=Storage.files.end();){if(it->first!=archivePath)it=Storage.files.erase(it);else ++it;}
 heap=1024*1024;nearHeap=false;readMs=0;fault.clear();epub.cssParser->contents.clear();epub.parseCssFiles();
 assert(epub.cssParser->contents.size()==size_t(cssCount+(mode=="duplicates"?1:0)));assert(liveArchives==0&&liveTemps==0);
 // Direct batch lifecycle: same object closes, sees a replacement archive, retries a failed scan.
 failAllocation=0;
 ZipFile zip(archivePath);assert(zip.open());
 assert(!zip.cacheSelectedFileStats(std::vector<std::string>(65,"OEBPS/style0.css")));
 assert(!zip.cacheSelectedFileStats({std::string(256,'a')}));
 assert(!zip.cacheSelectedFileStats(std::vector<std::string>(64,std::string(65,'a'))));
 assert(!zip.cacheSelectedFileStats({std::string("a\0b",3)}));
 std::vector<std::string> names={"OEBPS/style0.css"};assert(zip.cacheSelectedFileStats(names));
 size_t size=0;assert(zip.getInflatedFileSize(names[0].c_str(),&size));const size_t expectedSize=size;
 zip.close();auto replacement=std::make_shared<MemFile>();replacement->bytes={'n','o','t','z','i','p'};
 auto original=Storage.files[archivePath];Storage.files[archivePath]=replacement;assert(zip.open());assert(!zip.getInflatedFileSize(names[0].c_str(),&size));zip.close();
 Storage.files[archivePath]=original;assert(zip.open());fault="metadata";assert(!zip.cacheSelectedFileStats(names));zip.close();assert(zip.open());assert(zip.cacheSelectedFileStats(names));assert(zip.getInflatedFileSize(names[0].c_str(),&size)&&size==expectedSize);zip.close();
 assert(liveArchives==0&&liveTemps==0);
 std::printf("{\"cache_hex\":\"%s\",\"mode\":\"%s\",\"css\":%d,\"archive_entries_visited\":%llu,\"archive_read_calls\":%llu,\"archive_bytes_read\":%llu,\"seeks\":%llu,\"opens\":%llu,\"closes\":%llu,\"zip_yields\":%llu,\"warm_reads\":0}\n",cacheHex.c_str(),mode.c_str(),cssCount,(unsigned long long)cold.entries,(unsigned long long)cold.reads,(unsigned long long)cold.bytes,(unsigned long long)cold.seeks,(unsigned long long)cold.opens,(unsigned long long)cold.closes,(unsigned long long)cold.yields);
}
