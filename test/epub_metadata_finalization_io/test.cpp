#include "provider.inc"
#include <iterator>

static const std::string archive="/book.epub", cachePath="/cache", base="OPS/";
static std::ofstream snapshot;
static unsigned cases=0;
static void record(const void* p,size_t n) {
  const uint64_t length=n;snapshot.write(reinterpret_cast<const char*>(&length),sizeof(length));
  if(n)snapshot.write(static_cast<const char*>(p),n);
  assert(snapshot.good());
}
static void recordText(const std::string& s){record(s.data(),s.size());}
static void measure(bool retained=false) {
  if(!retained){assert(handles.empty());nextHandle=1;}
  counts={};byPath.clear();trace.clear();recording=true;
  Fixture::clockMs=Fixture::lastYield=Fixture::maxYieldGap=0;
  Fixture::delays=Fixture::yields=Fixture::locks=0;
  ready=true;openFails=closeFails=stickyError=syncFails=Fixture::lockFails=false;
  maxRead=maxWrite=errorCall=zeroCall=seekFail=dropMediaCall=dropWriteCall=shortWriteCall=SIZE_MAX;
  failOpenPath.clear();failClosePath.clear();latency=0;writeOnlyFaults=false;
}
static uint64_t waits(){return Fixture::delays+Fixture::yields;}
static void capture(const std::string& name,bool result) {
  ++cases;const auto stamp=Storage.generation();recordText(name);record(trace.data(),trace.size());
  const uint64_t summary[]={result,counts.reads,counts.bytes,counts.opens,counts.closes,counts.seeks,
    counts.errors,counts.writes,counts.writeBytes,counts.syncs,handles.size(),Fixture::locks,
    stamp.mount,stamp.mutation,stamp.quiescent};record(summary,sizeof(summary));
  for(const auto& f:files){recordText(f.first);record(f.second->data(),f.second->size());}
  for(const auto& d:dirs)recordText(d);
  for(const auto& h:handles){recordText(h.second.path);const uint64_t state[]={h.first,h.second.pos,h.second.error};record(state,sizeof(state));}
  recording=false;
}
static std::string chapter(unsigned n){char b[80];snprintf(b,sizeof(b),"OPS/ch%04u.xhtml",n);return b;}
static std::string topic(unsigned i,unsigned j){char b[80];snprintf(b,sizeof(b),"Topic %04u.%04u",i,j);return b;}
static std::string anchor(unsigned j){char b[80];snprintf(b,sizeof(b),"s%04u",j);return b;}

static BookMetadataCache::BookMetadata prepare(BookMetadataCache& cache,unsigned n,unsigned sections,size_t chunk) {
  ZipFile zip(archive);size_t size=0;BookMetadataCache::BookMetadata md;
  assert(zip.getInflatedFileSize("META-INF/container.xml",&size));ContainerParser container(size);
  assert(container.setup());assert(zip.readFileToStream("META-INF/container.xml",container,chunk));
  assert(container.fullPath=="OPS/content.opf");assert(cache.beginWrite()&&cache.beginContentOpfPass());
  std::string nav;
  {assert(zip.getInflatedFileSize(container.fullPath.c_str(),&size));ContentOpfParser opf(cachePath,base,size,&cache);
    assert(opf.setup());assert(zip.readFileToStream(container.fullPath.c_str(),opf,chunk));
    assert(cache.getSpineCount()==int(n));md={opf.title,opf.author,opf.language,opf.coverItemHref,opf.textReferenceHref};
    nav=opf.tocNavPath;assert(nav=="OPS/nav.xhtml");assert(md.title=="Reference Book"&&md.author=="Author"&&md.language=="en");}
  assert(cache.endContentOpfPass()&&cache.beginTocPass());
  {assert(zip.getInflatedFileSize(nav.c_str(),&size));TocNavParser parser(base,size,&cache);
    assert(parser.setup());assert(zip.readFileToStream(nav.c_str(),parser,chunk));}
  assert(cache.getTocCount()==int(n*sections));assert(cache.endTocPass()&&cache.endWrite());assert(handles.empty());return md;
}
static void decode(const Bytes& result,const BookMetadataCache::BookMetadata& md,unsigned n,unsigned sections) {
  // Production load() assumes 32-bit size_t for its LUT read, unlike this host.
  // Preserve producer source and independently decode its actual host ABI.
  recording=false;ZipFile zip(archive);size_t cursor=0;
  auto pod=[&](auto& v){assert(cursor+sizeof(v)<=result.size());memcpy(&v,result.data()+cursor,sizeof(v));cursor+=sizeof(v);};
  auto string=[&](){uint32_t len=0;pod(len);assert(len<=result.size()-cursor);std::string s((const char*)result.data()+cursor,len);cursor+=len;return s;};
  uint8_t version=0;uint32_t lut=0;uint16_t spines=0,toc=0;pod(version);pod(lut);pod(spines);pod(toc);
  assert(version==11&&spines==n&&toc==n*sections);
  assert(string()==md.title&&string()==md.author&&string()==md.language&&string()==md.coverItemHref&&string()==md.textReferenceHref);
  assert(cursor==lut);size_t cumulative=0;
  for(unsigned i=0;i<n;++i){cursor=lut+4*i;uint32_t off=0;pod(off);cursor=off;assert(string()==chapter(i));
    size_t total=0;int16_t tocIndex=-1;pod(total);pod(tocIndex);size_t part=0;
    assert(zip.getInflatedFileSize(chapter(i).c_str(),&part));cumulative+=part;assert(total==cumulative&&tocIndex==int(i*sections));}
  for(unsigned i=0;i<n;++i)for(unsigned j=0;j<sections;++j){cursor=lut+4*n+4*(i*sections+j);uint32_t off=0;pod(off);cursor=off;
    assert(string()==topic(i,j)&&string()==chapter(i)&&string()==anchor(j));uint8_t level=0;int16_t spineIndex=-1;
    pod(level);pod(spineIndex);assert(level==1&&spineIndex==int(i));}
  assert(cursor==result.size()&&handles.empty());
}
static Bytes healthy(BookMetadataCache& cache,const BookMetadataCache::BookMetadata& md,unsigned n,unsigned sections) {
  Bytes saved;uint64_t delays=0,yields=0;
  for(unsigned repeat=0;repeat<2;++repeat){
    measure();assert(cache.buildBookBin(archive,md)&&handles.empty());
    const auto toc=byPath.at(cachePath+"/toc.bin.tmp"),spine=byPath.at(cachePath+"/spine.bin.tmp"),book=byPath.at(cachePath+"/book.bin"),ar=byPath.at(archive);
    assert(toc.reads==24*n*sections&&spine.reads==(n>=128?12:8)*n&&book.writes==5*n+9*n*sections+12);
    assert(counts.opens==4&&counts.closes==4&&counts.syncs==0);
    const uint64_t ordinary=counts.reads+counts.writes+1;
    if(EXPECT_OPTIMIZED)assert(Fixture::delays<=ar.reads+ordinary/8+16&&"metadata scheduling bound");
    else assert(Fixture::delays==ordinary);
    if(!repeat){saved=*files.at(cachePath+"/book.bin");delays=Fixture::delays;yields=Fixture::yields;
      std::cout<<"spine="<<n<<" toc="<<n*sections<<" reads="<<counts.reads<<" writes="<<counts.writes
        <<" delays="<<delays<<" explicit_yields="<<yields<<" archive_reads="<<ar.reads<<" cache_bytes="<<saved.size()<<"\n";}
    else assert(saved==*files.at(cachePath+"/book.bin")&&Fixture::delays==delays&&Fixture::yields==yields);
    // Explicit preexisting CPU/ZIP yields are part of parity, budget delay()s are not.
    record(&yields,sizeof(yields));capture("healthy:"+std::to_string(repeat),true);
  }
  decode(saved,md,n,sections);return saved;
}
static void retry(BookMetadataCache& cache,const BookMetadataCache::BookMetadata& md,const Bytes& saved,const std::string& name,bool retained=false) {
  measure(retained);assert(cache.buildBookBin(archive,md)&&handles.empty()&&*files.at(cachePath+"/book.bin")==saved);capture(name,true);
}
static void metadataEdges(BookMetadataCache& cache,const BookMetadataCache::BookMetadata& md,const Bytes& saved,unsigned n,unsigned sections) {
  const uint64_t writes=5*n+9*n*sections+12;
  // Accepted caller-field boundary, separate from ordinary parser workloads.
  auto longMetadata=md;longMetadata.title=std::string(5000,'T');
  measure();assert(cache.buildBookBin(archive,longMetadata)&&handles.empty());
  const auto large=*files.at(cachePath+"/book.bin");capture("metadata-field-over-provider-chunk",true);
  decode(large,longMetadata,n,sections);retry(cache,md,saved,"metadata-field-boundary-retry");
  // All scratch reads remain complete and initialized. Do not inject failed
  // length reads into the existing unchecked serializer (separate BUG178).
  for(const auto& path:{cachePath+"/book.bin",cachePath+"/spine.bin.tmp",cachePath+"/toc.bin.tmp",archive}){
    measure();failOpenPath=path;assert(!cache.buildBookBin(archive,md)&&handles.empty());capture("open-failure:"+path,false);
    retry(cache,md,saved,"open-retry:"+path);
  }
  for(unsigned kind=0;kind<3;++kind){measure();if(kind==0)ready=false;if(kind==1)Fixture::lockFails=true;if(kind==2)Storage.markUnavailable();
    assert(!cache.buildBookBin(archive,md)&&handles.empty()&&!counts.reads&&!counts.writes);
    Fixture::lockFails=false;capture("admission:"+std::to_string(kind),false);
    ready=true;if(kind==2)assert(Storage.begin());retry(cache,md,saved,"admission-retry:"+std::to_string(kind));}
  // A missing archive is rejected without interpreting a partial ZIP header.
  auto epub=files.at(archive);files.erase(archive);measure();assert(!cache.buildBookBin(archive,md)&&handles.empty());
  capture("missing-zip",false);files[archive]=epub;retry(cache,md,saved,"missing-zip-retry");
  // Defined ZIP validation failures: below minimum EOCD size, or a complete
  // zero-filled EOCD-sized input. No partial read reaches a typed field.
  for(size_t length:{size_t(21),size_t(22)}){
    files[archive]=std::make_shared<Bytes>(length,0);measure();
    assert(cache.buildBookBin(archive,md)&&handles.empty()&&*files.at(cachePath+"/book.bin")!=saved);
    capture("zip-size-failure-ignored:"+std::to_string(length),true);
    files[archive]=epub;retry(cache,md,saved,"zip-size-retry:"+std::to_string(length));
  }
  // Write failures intentionally preserve the existing success return and
  // partial publication behavior. This test does not claim BUG178 is repaired.
  for(unsigned kind=0;kind<4;++kind)for(size_t call:{size_t(1),size_t(12),size_t(writes)}){
    measure();if(kind==0)zeroCall=call;if(kind==1)errorCall=call;if(kind==2){errorCall=call;stickyError=true;}if(kind==3)shortWriteCall=call;
    // Counters are operation-specific below: never inject corresponding read failures.
    writeOnlyFaults=true;
    assert(cache.buildBookBin(archive,md)&&handles.empty());writeOnlyFaults=false;
    capture("write-failure-ignored:"+std::to_string(kind)+":"+std::to_string(call),true);
    retry(cache,md,saved,"write-retry:"+std::to_string(kind)+":"+std::to_string(call));
  }
  measure();dropWriteCall=writes;assert(cache.buildBookBin(archive,md)&&handles.empty()&&!ready);
  capture("media-removed-after-final-write",true);retry(cache,md,saved,"media-final-write-retry");
  for(const auto& path:{cachePath+"/book.bin",cachePath+"/spine.bin.tmp",cachePath+"/toc.bin.tmp"}){
    measure();failClosePath=path;assert(cache.buildBookBin(archive,md)&&handles.size()==1&&!Storage.generation().quiescent);
    const auto retained=handles.begin()->first;capture("metadata-close-failure:"+path,true);
    measure(true);failClosePath=path;assert(!cache.buildBookBin(archive,md)&&handles.size()==1&&handles.count(retained));
    capture("metadata-close-still-retained:"+path,false);retry(cache,md,saved,"metadata-close-retry:"+path,true);
  }
  for(unsigned ms:{3u,9u})for(bool wrap:{false,true}){
    measure();latency=ms;if(wrap)Fixture::clockMs=Fixture::lastYield=UINT32_MAX-4ull;
    assert(cache.buildBookBin(archive,md)&&handles.empty()&&*files.at(cachePath+"/book.bin")==saved);
    if(EXPECT_OPTIMIZED)assert(Fixture::maxYieldGap<=8+ms);
    capture("metadata-elapsed:"+std::to_string(ms)+":"+std::to_string(wrap),true);
  }
}
static int readHal(FsFile& file,void* data,size_t size) {
  if(EXPECT_OPTIMIZED){HalReadBudget budget([](){return millis();},[](){delay(1);});return file.readCooperatively(data,size,budget);}
  return file.read(data,size);
}
static size_t writeHal(FsFile& file,const void* data,size_t size) {
#if HAL_HAS_WRITE_BUDGET
  if(EXPECT_OPTIMIZED){HalWriteBudget budget([](){return millis();},[](){delay(1);});return file.writeCooperatively(data,size,budget);}
#endif
  return file.write(data,size);
}
static void halEdges() {
  files.clear();dirs.clear();const std::string path="/hal-input";const Bytes data(9000,0x55);files[path]=std::make_shared<Bytes>(data);
  for(unsigned fault=0;fault<13;++fault){
    measure();FsFile file;assert(Storage.openFileForRead("test",path,file));
    Bytes buffer(9000,0xcc);void* output=buffer.data();size_t size=buffer.size();
    if(fault==0)maxRead=7;
    if(fault==1)zeroCall=1;
    if(fault==2)zeroCall=2;
    if(fault==3)errorCall=1;
    if(fault==4)errorCall=2;
    if(fault==5){latency=5000;maxRead=1;size=10;}
    if(fault==6)size=16u*1024u*1024u+1u;
    if(fault==7)output=nullptr;
    if(fault==8)size=0;
    if(fault==9)ready=false;
    if(fault==10)Fixture::lockFails=true;
    if(fault==11){zeroCall=1;latency=8;}
    if(fault==12){errorCall=1;latency=8;Fixture::clockMs=Fixture::lastYield=UINT32_MAX-4ull;}
    const int result=readHal(file,output,size);
    const int expected=fault==0?9000:fault==1||fault==8||fault==11?0:fault==2||fault==4?4096:fault==5?4:-1;
    assert(result==expected);
    if(fault==5)assert(file.getError()&&counts.reads==4&&counts.bytes==4);
    if(EXPECT_OPTIMIZED&&(fault==11||fault==12))assert(waits()==1);
    Fixture::lockFails=false;ready=true;const auto error=file.getError();assert(file.close()&&handles.empty());
    record(buffer.data(),buffer.size());record(&result,sizeof(result));record(&error,sizeof(error));
    capture("hal-read:"+std::to_string(fault),true);
  }
  for(unsigned fault=0;fault<17;++fault){
    measure();auto file=Storage.open("/hal-output",O_RDWR|O_CREAT|O_TRUNC|((fault==11||fault==12)?O_SYNC:0));assert(file);
    const void* input=data.data();size_t size=data.size();
    if(fault==0)maxWrite=7;
    if(fault==1)zeroCall=1;
    if(fault==2)zeroCall=2;
    if(fault==3)errorCall=1;
    if(fault==4)errorCall=2;
    if(fault==5)latency=10000;
    if(fault==6)size=16u*1024u*1024u+1u;
    if(fault==7)input=nullptr;
    if(fault==8)size=0;
    if(fault==9)ready=false;
    if(fault==10)Fixture::lockFails=true;
    if(fault==11)syncFails=true;
    if(fault==13){zeroCall=1;latency=8;}
    if(fault==14){errorCall=1;latency=8;Fixture::clockMs=Fixture::lastYield=UINT32_MAX-4ull;}
    if(fault==15){assert(file.close());}
    if(fault==16){assert(file.close());file=Storage.open("/hal-output",O_RDONLY);assert(file);}
    const auto result=writeHal(file,input,size);
    const size_t expected=fault==0?7:fault==2||fault==3||fault==11||fault==14?4096:fault==4||fault==5?8192:fault==12?9000:0;
    assert(result==expected);
    if(fault==5)assert(file.getError()&&counts.writes==2&&counts.writeBytes==8192);
    if(EXPECT_OPTIMIZED&&(fault==13||fault==14))assert(waits()==1);
    Fixture::lockFails=false;ready=true;const auto error=file.getError();assert(file.close()&&handles.empty());
    record(&result,sizeof(result));record(&error,sizeof(error));capture("hal-write:"+std::to_string(fault),true);
  }
  // A failed sync and a subsequent retry retain the existing generation/error contract.
  measure();{auto file=Storage.open("/hal-output",O_RDWR);assert(file);syncFails=true;file.flush();assert(file.getError());
    syncFails=false;file.flush();assert(file.getError()&&file.close());}capture("flush-failure-retry",true);
#if HAL_HAS_WRITE_BUDGET
  // Primitive thresholds are checked without adding variant-specific I/O to the snapshot.
  for(bool write:{false,true})for(unsigned scenario=0;scenario<6;++scenario){
    measure();if(scenario==3)Fixture::clockMs=Fixture::lastYield=UINT32_MAX-3ull;
    HalReadBudget read([](){return millis();},[](){delay(1);});HalWriteBudget output([](){return millis();},[](){delay(1);});
    const auto after=[&](size_t n){if(write)output.afterWrite(n);else read.afterRead(n);};
    const auto check=[&](){if(write)output.checkpoint();else read.checkpoint();};
    if(scenario==0){for(unsigned i=0;i<31;++i)after(1);assert(!waits());after(1);}
    if(scenario==1){after(4095);assert(!waits());after(1);}
    if(scenario==2){Fixture::clockMs=7;check();assert(!waits());Fixture::clockMs=8;check();}
    if(scenario==3){Fixture::clockMs+=8;check();}
    if(scenario==4){Fixture::clockMs+=8;check();check();}
    if(scenario==5){Fixture::clockMs+=8;check();after(4095);assert(waits()==1);after(1);assert(waits()==2);continue;}
    assert(waits()==1);
  }
#endif
  recording=false;
}

static void ordinaryAndRetained() {
  measure();files["/ordinary-input"]=std::make_shared<Bytes>(128,0x5a);
  {FsFile f;assert(Storage.openFileForRead("test","/ordinary-input",f));
    for(unsigned i=0;i<100;++i){uint8_t b=0;assert(f.read(&b,1)==1&&b==0x5a);}assert(f.close());}
  assert(waits()==100);capture("ordinary-unbudgeted-read",true);
  measure();{auto f=Storage.open("/ordinary-output",O_RDWR|O_CREAT|O_TRUNC);assert(f);
    for(unsigned i=0;i<100;++i){assert(f.write(static_cast<uint8_t>(i))==1);}assert(f.close());}
  assert(waits()==100);capture("ordinary-unbudgeted-write",true);
  measure();{auto f=Storage.open("/ordinary-output",O_RDWR);assert(f);closeFails=true;
    assert(!f.close()&&handles.size()==1);closeFails=false;assert(f.close()&&handles.empty());}
  capture("explicit-handle-close-retry",true);
  measure();{auto f=Storage.open("/ordinary-output",O_RDWR);assert(f);closeFails=true;}
  assert(handles.size()==1&&!Storage.generation().quiescent);const auto retained=handles.begin()->first;
  capture("destructor-close-retains-slot",true);
  measure(true);{auto f=Storage.open("/ordinary-input");assert(f);uint8_t b=0;assert(f.read(&b,1)==1&&f.close());}
  assert(handles.size()==1&&handles.count(retained));capture("operation-after-retained-slot",true);
  recording=false;assert(providerClose(nullptr,retained,true)); // Provider fixture teardown only.
}
int main(int argc,char**argv) {
  assert(argc==7);const unsigned n=std::stoul(argv[2]),sections=std::stoul(argv[3]);const size_t chunk=std::stoul(argv[4]);
  const bool edges=std::string(argv[6])=="edges";
  snapshot.open(argv[5],std::ios::binary);assert(snapshot.good());assert(Storage.bindVolume(&volume.base));
  std::ifstream in(argv[1],std::ios::binary);assert(in);
  files[archive]=std::make_shared<Bytes>(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
  const auto originalArchive=*files.at(archive);BookMetadataCache cache(cachePath);
  measure();const auto md=prepare(cache,n,sections,chunk);
  // Capture setup scheduling too: parsers, ordinary entry helpers and ZIP stay unbudgeted.
  const uint64_t preparationWaits[]={Fixture::delays,Fixture::yields};record(preparationWaits,sizeof(preparationWaits));capture("parser-created-scratch",true);
  const auto saved=healthy(cache,md,n,sections);
  if(edges)metadataEdges(cache,md,saved,n,sections);
  measure();assert(cache.cleanupTmpFiles());
  assert(files.count(cachePath+"/spine.bin.tmp")==0&&files.count(cachePath+"/toc.bin.tmp")==0);
  assert(files.size()==2&&*files.at(archive)==originalArchive&&*files.at(cachePath+"/book.bin")==saved&&handles.empty());
  capture("temporary-cleanup",true);
  if(edges){halEdges();ordinaryAndRetained();}
  snapshot.close();std::cout<<"PASS: "<<cases<<" deterministic metadata/HAL snapshots; host size_t="<<sizeof(size_t)<<"\n";
}
