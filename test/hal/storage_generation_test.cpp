#define HAL_STORAGE_IMPL
#include <Board.h>
#include <HalStorage.h>
#include <HalStorageLifecycle.h>
#include <SdFat.h>

#include <cassert>
#include <iostream>
int main() {
  StorageGenerationTracker exhausted(UINT64_MAX);
  exhausted.mutationAttempt();
  assert(!exhausted.stamp(true).quiescent);
  assert(!exhausted.mountAttempt());
  assert(!Storage.generation().quiescent);
  assert(Storage.begin());
  auto initial = Storage.generation();
  assert(initial.quiescent);
  assert(Storage.mkdir("/Apps"));
  assert(!Storage.unchanged(initial));
  {
    auto file = Storage.open("/Apps/test", O_WRONLY | O_CREAT);
    assert(file.isOpen() && !Storage.generation().quiescent);
    auto before = Storage.generation();
    assert(file.write("a", 1) == 1);
    assert(Storage.generation().mutation > before.mutation);
    before = Storage.generation();
    assert(file.write(uint8_t('b')) == 1);
    assert(Storage.generation().mutation > before.mutation);
    auto moved = std::move(file);
    assert(!file.isOpen() && !Storage.generation().quiescent);
    const auto mounts = FakeSd::mounts;
    before = Storage.generation();
    assert(!Storage.begin());
    assert(FakeSd::mounts == mounts && !Storage.unchanged(before));
    moved.flush();
    assert(moved.close());
    assert(Storage.generation().quiescent);
  }
  auto stamp = Storage.generation();
  {
    auto reader = Storage.open("/Apps/test");
    assert(reader.isOpen() && reader.fileSize64() == 2);
    char data[3]{};
    assert(reader.read(data, 2) == 2);
    assert(Storage.unchanged(stamp));
    assert(!Storage.begin());
    assert(reader.close());
  }
  assert(Storage.begin());
  stamp = Storage.generation();
  assert(!Storage.remove("/missing"));
  assert(!Storage.unchanged(stamp));
  {
    auto a = Storage.open("/outside", O_WRONLY | O_CREAT);
    auto b = Storage.open("/Apps/other", O_WRONLY | O_CREAT);
    const auto closed = FakeSd::closedWrites;
    b = std::move(a);
    assert(FakeSd::closedWrites == closed + 1);
    assert(Storage.rename("/outside", "/Apps/moved"));
    stamp = Storage.generation();
    assert(b.write("z", 1) == 1);
    assert(Storage.generation().mutation > stamp.mutation);
  }
  assert(Storage.generation().quiescent);
  stamp = Storage.generation();
  assert(Storage.writeFile("/data/raw", "hello"));
  assert(!Storage.unchanged(stamp));
  {
    HalFile output;
    assert(Storage.openFileForWrite("test", "/data/helper", output));
    assert(!Storage.generation().quiescent);
    assert(Storage.openFileForRead("test", "/Apps/test", output));
    assert(Storage.generation().quiescent);
    assert(output.close());
  }
  {
    auto bad = Storage.open("/Apps/error", O_WRONLY | O_CREAT);
    FakeSd::failClose = "/apps/error";
    assert(!bad.close());
    assert(!Storage.generation().quiescent);
    FakeSd::failClose.clear();
    assert(bad.close());
  }
  {
    auto bad = Storage.open("/Apps/error", O_WRONLY);
    FakeSd::failWrite = "/apps/error";
    stamp = Storage.generation();
    assert(bad.write("oops", 4) == 2);
    assert(Storage.generation().mutation > stamp.mutation);
    FakeSd::failWrite.clear();
  }
  {
    auto bad = Storage.open("/Apps/test");
    FakeSd::failRead = "/apps/test";
    stamp = Storage.generation();
    char c;
    assert(bad.read(&c, 1) < 0);
    assert(!Storage.unchanged(stamp));
    FakeSd::failRead.clear();
  }
  {
    auto bad = Storage.open("/Apps/test");
    FakeSd::failSeek = "/apps/test";
    stamp = Storage.generation();
    assert(!bad.seek64(0));
    assert(!Storage.unchanged(stamp));
    FakeSd::failSeek.clear();
    assert(bad.close());
  }
  {
    auto bad = Storage.open("/Apps/test");
    FakeSd::failName = "/apps/test";
    char name[20]{};
    stamp = Storage.generation();
    assert(!bad.getName(name, sizeof(name)));
    assert(!Storage.unchanged(stamp));
    FakeSd::failName.clear();
    assert(bad.close());
  }
  halStorageMediaUnavailable();
  assert(!Storage.ready() && !Storage.generation().quiescent);
  assert(Storage.begin());
  stamp = Storage.generation();
  assert(Storage.removeDir("/data"));
  assert(!Storage.unchanged(stamp));
  FakeSd::mountOkay = false;
  stamp = Storage.generation();
  assert(!Storage.begin());
  assert(!Storage.ready() && !Storage.unchanged(stamp));
  FakeSd::mountOkay = true;
  assert(Storage.begin());
  stamp = Storage.generation();
  Storage.externalStorageBegin();
  assert(!Storage.generation().quiescent);
  assert(!Storage.begin());
  Storage.externalStorageEnd(true);
  assert(!Storage.generation().quiescent && !Storage.unchanged(stamp));
  auto retained = Storage.open("/Apps/test");
  assert(!Storage.reconcileExternalStorage());
  assert(retained.close());
  FakeSd::mountOkay = false;
  assert(!Storage.reconcileExternalStorage() && !Storage.generation().quiescent);
  FakeSd::mountOkay = true;
  assert(Storage.reconcileExternalStorage() && Storage.generation().quiescent);
  Storage.externalStorageBegin();
  Storage.externalStorageUncertain();
  Storage.externalStorageEnd(true);
  assert(!Storage.generation().quiescent && !Storage.begin());
  std::cout << "Production HalStorage: attempts, writers, moves/destructors, locks, read faults, remounts, external "
               "windows and exhaustion PASS\n";
}
