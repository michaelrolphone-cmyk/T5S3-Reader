#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <sstream>

static size_t failAllocationBytes=0;
static unsigned failAllocationNumber=0;
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (size==failAllocationBytes) return nullptr;
  if (failAllocationNumber && --failAllocationNumber==0) return nullptr;
  return ::operator new[](size);
}
static std::ostringstream snapshot;
static std::string encode(uint32_t cp) {
  if(cp<128)return std::string(1,static_cast<char>(cp));
  if(cp<2048)return {char(0xc0|(cp>>6)),char(0x80|(cp&63))};
  if(cp<65536)return {char(0xe0|(cp>>12)),char(0x80|((cp>>6)&63)),char(0x80|(cp&63))};
  return {char(0xf0|(cp>>18)),char(0x80|((cp>>12)&63)),char(0x80|((cp>>6)&63)),char(0x80|(cp&63))};
}
static std::vector<uint32_t> collect(const std::vector<std::string>& words,uint32_t cap,bool& hit) {
  std::vector<uint32_t> actual(cap+2);
  uint32_t count=0;hit=false;
#ifdef INDEXED_COLLECTION
  CodepointScanProgress progress;
#endif
  for(const auto& word:words) {
#ifdef INDEXED_COLLECTION
    hit=collectUniqueCodepoints(word.c_str(),actual.data(),count,cap,progress);
#else
    hit=collectUniqueCodepoints(word.c_str(),actual.data(),count,cap);
#endif
    if(hit)break;
  }
  actual.resize(count);std::sort(actual.begin(),actual.end());return actual;
}
static void checkCollection(const std::vector<std::string>& words,uint32_t cap) {
  std::set<uint32_t> expected;bool expectedHit=false;
  for(const auto& word:words) {
    auto p=reinterpret_cast<const unsigned char*>(word.c_str());
    while(*p) {
      const auto cp=utf8NextCodepoint(&p);if(!cp)break;
      if(!expected.count(cp)) {if(expected.size()==cap){expectedHit=true;break;}expected.insert(cp);}
    }
    if(expectedHit)break;
  }
  bool hit;const auto actual=collect(words,cap,hit);
  assert(hit==expectedHit);assert(actual==std::vector<uint32_t>(expected.begin(),expected.end()));
  snapshot<<"set "<<cap<<' '<<hit;for(auto cp:actual)snapshot<<' '<<cp;snapshot<<'\n';
}
static void ioReset() {reads=bytes=opens=seeks=closes=0;membershipComparisons=0;yields=0;}
static void saveAdvances(SdCardFont& font,int result) {
  snapshot<<"adv "<<result<<' '<<reads<<' '<<bytes<<' '<<opens<<' '<<seeks<<' '<<closes;
  for(unsigned style=0;style<4;++style) for(unsigned cp=0x4e00;cp<0x4e00+1024;++cp)
    snapshot<<' '<<font.getAdvance(cp,style);
  snapshot<<' '<<font.getAdvance(' ',0)<<' '<<font.getAdvance('-',0)<<'\n';
}
int main(int argc,char**argv) {
  assert(argc==2);Storage.root=argv[1];
  for(unsigned cap:{0u,1u,8u,4096u}) {
    checkCollection({},cap);checkCollection({"","abc","cba"," ","-"},cap);
    checkCollection({"A\x80\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80",std::string("x\0y",3),"\xe2\x82"},cap);
    std::vector<uint32_t> cps(4100);std::iota(cps.begin(),cps.end(),0x4e00);
    for(unsigned order=0;order<3;++order) {
      if(order==1)std::reverse(cps.begin(),cps.end());
      if(order==2){std::mt19937 gen(42);std::shuffle(cps.begin(),cps.end(),gen);}
      std::vector<std::string> words;for(auto cp:cps)words.push_back(encode(cp));
      words.insert(words.begin()+std::min<size_t>(cap,words.size()),encode(cps[0]));
      checkCollection(words,cap);
    }
  }
  for(unsigned unique:{64u,128u,256u,512u}) for(unsigned repeat:{1u,4u,16u,64u}) {
    realClock=true;
    SdCardFont font;assert(font.load("/SD_fonts/SourceHanSansSC/SourceHanSansSC_14.cpfont"));
    std::vector<std::string> words;std::string word;
    for(unsigned i=0;i<unique*repeat;++i){word+=encode(0x4e00+i%unique);if(word.size()==198){words.push_back(word);word.clear();}}
    if(!word.empty())words.push_back(word);
    assert(words.size()<=750);
    for(unsigned warm=0;warm<2;++warm) {
      ioReset();const int result=font.buildAdvanceTable(words,false,1);assert(result==0);
      for(unsigned i=0;i<unique;++i)assert(font.getAdvance(0x4e00+i,0)>0);
      if(warm)assert(reads==0 && opens==0 && seeks==0);
#ifdef EXPECT_BASELINE
      const uint64_t expected=uint64_t(unique)*(unique-1)/2+uint64_t(repeat-1)*unique*(unique+1)/2;
      assert(membershipComparisons==expected);
#else
      if(membershipComparisons>uint64_t(unique)*repeat*11){std::cerr<<"membership cost bound failed: "<<membershipComparisons<<'\n';return 1;}
#endif
      saveAdvances(font,result);
      std::cout<<"unique="<<unique<<" repeat="<<repeat<<" warm="<<warm<<" comparisons="<<membershipComparisons<<" reads="<<reads<<" yields="<<yields<<'\n';
    }
  }
  {
    // Above-cap public input still adds reserved punctuation, keeps the first
    // admitted set, and preserves the existing cache-saturation behavior.
    SdCardFont font;assert(font.load("/SD_fonts/SourceHanSansSC/SourceHanSansSC_14.cpfont"));
    std::vector<unsigned> points(4100);std::iota(points.begin(),points.end(),0x4e00);
    std::mt19937 gen(77);std::shuffle(points.begin(),points.end(),gen);
    std::vector<std::string> words;for(auto cp:points)words.push_back(encode(cp));
    ioReset();saveAdvances(font,font.buildAdvanceTable(words,true,15));
    assert(font.getAdvance(' ',0)>0 && font.getAdvance('-',0)>0);
    ioReset();saveAdvances(font,font.buildAdvanceTable(words,true,15));
  }
  {
    SdCardFont font;assert(font.buildAdvanceTable("A",1)==-1);
    assert(font.load("/SD_fonts/SourceHanSansSC/SourceHanSansSC_14.cpfont"));
    const std::vector<std::string> words={encode(0x4e02),encode(0x4e00),encode(0x4e01),"\xe3\x80\xaa","\xef\xbf\xbf"};
    for(unsigned fault=0;fault<10;++fault) {
      font.clearCache();font.clearPersistentCache();ioReset();
      if(fault==1)failOpen=true;
      if(fault==2)failRead=1;
      if(fault==3)failRead=2;
      if(fault==4)failSeek=1;
      if(fault==5)failAllocationBytes=(4096+2)*sizeof(uint32_t);
      if(fault==6)failShortRead=2;
      if(fault>=7)failAllocationNumber=fault-5; // mappings, staging, persistent table
      const int result=font.buildAdvanceTable(words,true,15);
      if(fault==5)assert(result==-1 && reads==0 && opens==0);
      assert(liveFiles==0);saveAdvances(font,result);
      failOpen=false;failRead=failShortRead=failSeek=0;failAllocationBytes=0;failAllocationNumber=0;
      ioReset();assert(font.buildAdvanceTable(words,true,15)>=0);saveAdvances(font,0);
      assert(font.getAdvance(0x4e00,0)>0 && font.getAdvance(' ',0)>0 && font.getAdvance('-',0)>0);
      ioReset();assert(font.buildAdvanceTable(words,true,0)==0);assert(opens==0 && reads==0);
    }
    font.clearCache();font.clearPersistentCache();assert(!font.hasAdvanceTable());
    assert(!font.load("/absent"));assert(font.buildAdvanceTable("A",1)==-1);
    assert(font.load("/SD_fonts/SourceHanSansSC/SourceHanSansSC_14.cpfont"));
    assert(font.buildAdvanceTable(encode(0x4e00).c_str(),1)==0);
  }
  assert(liveFiles==0);
#ifdef INDEXED_COLLECTION
  realClock=false;
  for(unsigned long step:{0ul,11ul}) {
    clockMs=ULONG_MAX-25;clockStep=step;yields=0;bool hit;
    const auto result=collect(std::vector<std::string>(8192,"a"),4096,hit);
    assert(result==std::vector<uint32_t>{'a'} && !hit);
    if(step==0)assert(yields==0);else assert(yields>=4);
    std::cout<<"checkpoint clock_step="<<step<<" yields="<<yields<<'\n';
  }
  clockStep=0;
#endif
  if(const char* path=std::getenv("CODEPOINT_SNAPSHOT")){std::ofstream out(path,std::ios::binary);out<<snapshot.str();assert(out.good());}
  std::cout<<"exact set/cap/UTF8, cold/warm metrics, styles, allocation/I/O failure, retry, reload, cleanup and yields PASS\n";
}
