void write(const std::string& path, const std::string& data) {
  auto file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  assert(file.isOpen() && file.write(data.data(), data.size()) == data.size() && file.close());
}
void package(const std::string& id, const char* capability = "test.metadata") {
  const auto path = "/Drivers/" + id;
  assert(Storage.mkdir(path.c_str()));
  write(path + "/provider-abi.v1", std::string("os-cpu-abi=1\nprovides=") + capability + "\napi=1\n");
  write(path + "/.package.json", "valid");
}
bool absent() {
  size_t cursor=0; char id[96]{};
  return !nextProvider("serial.port",1,&cursor,id,sizeof(id)) && cursor != SIZE_MAX;
}
void fault() {
  size_t cursor=0; char id[96]{};
  assert(!nextProvider("serial.port",1,&cursor,id,sizeof(id)) && cursor == SIZE_MAX);
}
void warm() {
  const auto opens=FakeSd::opens, reads=FakeSd::reads, yields=enumerationYields, checked=inspections;
  for (unsigned n=0;n<10;++n) assert(absent());
  assert(FakeSd::opens==opens && FakeSd::reads==reads && enumerationYields==yields && inspections==checked);
}
void cold() {
  const auto opens=FakeSd::opens, checked=inspections;
  assert(absent()); assert(FakeSd::opens>opens && inspections>checked); warm();
}
void mutateAtYield() {
  enumerationYieldHook=nullptr;
  assert(!FakeLock::held);
  Storage.invalidateObservations();
}
int main(int argc,char**) {
  std::setvbuf(stdout,nullptr,_IONBF,0);
  assert(Storage.begin()); assert(Storage.mkdir("/Drivers"));
  for(unsigned n=0;n<8;++n)package("fixture-"+std::to_string(n));
  cold(); // The original production function fails the first warm assertion.
  {
    const auto opens=FakeSd::opens, reads=FakeSd::reads;
    size_t one=0,two=0;char name[96]{};
    assert(nextProvider("test.metadata",1,&one,name,sizeof(name))&&!std::strcmp(name,"fixture-0"));
    assert(!nextProvider("test.metadata",2,&two,name,sizeof(name))&&two!=SIZE_MAX);
    assert(nextProvider("test.metadata",1,&one,name,sizeof(name))&&!std::strcmp(name,"fixture-1"));
    assert(FakeSd::opens==opens&&FakeSd::reads==reads);
  }
  std::puts("Unchanged warm metadata: zero opens, reads, inspections and waits PASS");
  write("/Drivers/fixture-0/.package.json","valid"); cold();
  assert(Storage.begin()); cold();
  auto writer=Storage.open("/writer",O_WRONLY|O_CREAT);assert(writer);
  for(unsigned n=0;n<2;++n){auto before=FakeSd::opens;assert(absent());assert(FakeSd::opens>before);}
  assert(writer.close()); cold();
  Storage.externalStorageBegin();
  for(unsigned n=0;n<2;++n){auto before=FakeSd::opens;assert(absent());assert(FakeSd::opens>before);}
  Storage.externalStorageEnd(true);assert(Storage.reconcileExternalStorage());cold();
  Storage.invalidateObservations();enumerationYieldHook=mutateAtYield;fault();cold();
  std::puts("Mutation, remount, writer/raw windows and mid-scan mutation PASS");
  Storage.invalidateObservations();FakeSd::failRead="/drivers/fixture-2/provider-abi.v1";fault();
  FakeSd::failRead.clear();cold();
  Storage.invalidateObservations();FakeSd::failDirectory="/drivers";fault();
  FakeSd::failDirectory.clear();cold();
  Storage.invalidateObservations();FakeSd::failOpen="/providers";FakeSd::mediaError=1;fault();
  FakeSd::failOpen.clear();FakeSd::mediaError=0;cold();
  write("/Drivers/fixture-2/.package.json","bad");fault();
  write("/Drivers/fixture-2/.package.json","valid");cold();
  write("/Providers","not-a-directory");fault();assert(Storage.remove("/Providers"));cold();
  std::puts("Read/directory/open/metadata/root-type failures and retry PASS");
  // A cached positive candidate must also disappear after a namespace change.
  package("serial", "serial.port");size_t cursor=0;char id[96]{};
  assert(nextProvider("serial.port",1,&cursor,id,sizeof(id)) && !std::strcmp(id,"serial"));
  Storage.invalidateObservations();
  assert(!nextProvider("serial.port",1,&cursor,id,sizeof(id)) && cursor==SIZE_MAX);
  write("/Drivers/serial/provider-abi.v1","os-cpu-abi=1\nprovides=test.changed\napi=1\n");cold();
  package("serial-a","serial.port");package("serial-b","serial.port");
  size_t cursorA=0,cursorB=0;
  assert(nextProvider("serial.port",1,&cursorA,id,sizeof(id))&&!std::strcmp(id,"serial-a"));
  assert(Storage.removeDir("/Drivers/serial-a"));package("serial-c","serial.port");
  assert(nextProvider("serial.port",1,&cursorB,id,sizeof(id))&&!std::strcmp(id,"serial-b"));
  assert(!nextProvider("serial.port",1,&cursorA,id,sizeof(id))&&cursorA==SIZE_MAX);
  assert(nextProvider("serial.port",1,&cursorB,id,sizeof(id))&&!std::strcmp(id,"serial-c"));
  // Independent cursors may share one unchanged snapshot.
  cursorA=cursorB=0;
  assert(nextProvider("serial.port",1,&cursorA,id,sizeof(id))&&!std::strcmp(id,"serial-b"));
  assert(nextProvider("serial.port",1,&cursorB,id,sizeof(id))&&!std::strcmp(id,"serial-b"));
  assert(nextProvider("serial.port",1,&cursorA,id,sizeof(id))&&!std::strcmp(id,"serial-c"));
  assert(nextProvider("serial.port",1,&cursorB,id,sizeof(id))&&!std::strcmp(id,"serial-c"));
  // Even a failed B rebuild invalidates A rather than reporting false EOF.
  Storage.invalidateObservations();FakeSd::failRead="/drivers/serial-b/provider-abi.v1";fault();
  assert(!nextProvider("serial.port",1,&cursorA,id,sizeof(id))&&cursorA==SIZE_MAX);
  FakeSd::failRead.clear();
  assert(Storage.removeDir("/Drivers/serial-b")&&Storage.removeDir("/Drivers/serial-c"));cold();
  std::puts("Opaque cursors reject successful/failed replacement snapshots PASS");
  // Exactly 64 root entries is complete; a 65th must never be cached as EOF.
  for(unsigned n=9;n<64;++n)write("/Drivers/ignored-"+std::to_string(n),"x");
  cold();write("/Drivers/ignored-overflow","x");fault();
  assert(Storage.remove("/Drivers/ignored-overflow"));cold();
  for(unsigned n=9;n<64;++n)assert(Storage.remove(("/Drivers/ignored-"+std::to_string(n)).c_str()));
  for(unsigned n=9;n<17;++n)package("provider-"+std::to_string(n));
  fault(); // More than the fixed 16 metadata candidates is not a partial success.
  assert(Storage.removeDir("/Drivers/provider-16"));cold();
  std::puts("Positive-cursor invalidation and exact directory/candidate bounds PASS");
  if(argc>1) {
    Storage.invalidateObservations();FakeSd::failClose="/drivers";fault();
    FakeSd::failClose.clear();
    // Discarded uncertain ownership permanently forbids retained reuse.
    assert(!Storage.generation().quiescent);
    auto before=FakeSd::opens;assert(absent());assert(FakeSd::opens>before);
    before=FakeSd::opens;assert(absent());assert(FakeSd::opens>before);
    std::puts("Failed close retains uncertainty and cannot reuse a snapshot PASS");
  }
}
