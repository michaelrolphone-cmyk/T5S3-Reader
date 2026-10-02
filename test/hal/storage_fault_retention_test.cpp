#define HAL_STORAGE_IMPL
#include <Board.h>
#include <HalStorage.h>
#include <SdFat.h>
#include "SdSpiFault.h"
#include <cassert>
#include <cstdio>
static bool fault;
extern "C" bool risc_sd_spi_faulted() { return fault; }
extern "C" void risc_sd_spi_guard() { assert(!fault); }
extern "C" void risc_sd_spi_begin_operation() { assert(!fault); }
extern "C" void risc_sd_spi_end_operation() { assert(!fault); }
extern "C" void risc_sd_spi_wait_lock(void* mutex) {
  assert(!fault); assert(xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex), 30000));
}
int main() {
  assert(Storage.begin());
  const auto before=Storage.generation();
  {
    auto first=Storage.open("/first", O_WRONLY|O_CREAT);
    auto second=Storage.open("/second", O_WRONLY|O_CREAT);
    assert(first.isOpen() && second.isOpen());
    const auto closed=FakeSd::closedWrites, opens=FakeSd::opens, mounts=FakeSd::mounts;
    const auto locks=FakeLock::acquisitions;
    fault=true;
    assert(!Storage.ready() && !Storage.unchanged(before) && !Storage.generation().quiescent);
    assert(!Storage.begin() && !Storage.reconcileExternalStorage());
    assert(!Storage.open("/new") && !Storage.remove("/first") && !Storage.mkdir("/new"));
    char c=0;
    assert(first.read(&c,1)==-1 && first.write("x",1)==0 && !first.close());
    assert(first.getError()!=0 && !first.isOpen());
    first.flush(); first.rewindDirectory();
    second=std::move(first); // Both previous and newly adopted raw handles retained.
    assert(FakeSd::closedWrites==closed && FakeSd::opens==opens && FakeSd::mounts==mounts);
    assert(FakeLock::acquisitions==locks && !FakeLock::held);
  }
  assert(FakeSd::closedWrites==0); // No raw destructor sync/unlock during teardown.
  std::puts("Actual HalStorage permanent fault: unavailable, no new I/O, close/move/destructor retention PASS");
}
