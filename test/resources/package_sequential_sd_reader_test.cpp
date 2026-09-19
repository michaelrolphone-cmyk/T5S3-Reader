#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include "runtime/packages/PackageSequentialSdReader.h"

int main() {
  fakeSd.files["/Drivers/a/driver.elf"] = {1, 2, 3, 4, 5, 6, 7, 8};
  fakeSd.files["/Drivers/b/driver.elf"] = {9, 10, 11, 12};
  uint8_t bytes[8]{};
  {
    RuntimePackages::OrdinarySequentialSdReader reader;
    assert(reader.readAt("/Drivers/a/driver.elf", 0, bytes, 2));
    assert(reader.readAt("/Drivers/a/driver.elf", 2, bytes + 2, 2));
    assert(reader.readAt("/Drivers/a/driver.elf", 4, bytes + 4, 2));
    assert(fakeSd.opens == 1 && fakeSd.closes == 0);
    assert(reader.readAt("/Drivers/a/driver.elf", 6, bytes + 6, 2));
    assert(fakeSd.opens == 1 && fakeSd.closes == 1);
    for (int i = 0; i < 8; ++i) assert(bytes[i] == i + 1);
    assert(reader.readAt("/Drivers/b/driver.elf", 0, bytes, 4));
    assert(fakeSd.opens == 2 && fakeSd.closes == 2);
    assert(bytes[0] == 9 && bytes[3] == 12);
    assert(!reader.readAt("/Drivers/b/driver.elf", 2, bytes, 2));
    assert(fakeSd.opens == 2);
  }
  {
    RuntimePackages::OrdinarySequentialSdReader reader;
    assert(reader.readAt("/Drivers/a/driver.elf", 0, bytes, 2));
    assert(!reader.readAt("/Drivers/a/driver.elf", 3, bytes, 2));
    assert(!reader.readAt("/Drivers/b/driver.elf", 2, bytes, 2));
    assert(!reader.readAt("/Drivers/a/driver.elf", 2, bytes, 7));
    // Aborted verification must close its descriptor on destruction.
  }
  assert(fakeSd.opens == 3 && fakeSd.closes == 3);
  {
    RuntimePackages::OrdinarySequentialSdReader reader;
    fakeSd.failRead = true;
    assert(!reader.readAt("/Drivers/a/driver.elf", 0, bytes, 2));
    assert(fakeSd.closes == 4);
    fakeSd.failRead = false;
    fakeSd.failClose = true;
    assert(!reader.readAt("/Drivers/b/driver.elf", 0, bytes, 4));
    fakeSd.failClose = false;
  }
  assert(fakeSd.opens == 5 && fakeSd.closes == 5);
  return 0;
}
