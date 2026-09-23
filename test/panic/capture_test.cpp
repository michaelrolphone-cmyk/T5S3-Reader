#include "PanicCapture.h"
#include <cassert>
#include <cstdio>
#include <array>

int main() {
  constexpr uint32_t low = 0x1000, high = 0x2000;
  std::array<uint32_t, (high-low)/4> memory{};
  size_t reads = 0;
  auto read = [&](uint32_t address) {
    assert(PanicCapture::readable(address, 4, low, high));
    ++reads;
    return memory[(address-low)/4];
  };
  auto put = [&](uint32_t address, uint32_t value) { memory[(address-low)/4] = value; };
  volatile PanicCapture::Record record{};
  put(0x1100-12, 0x1140); put(0x1100-16, 0x82002003);
  put(0x1140-12, 0x1180); put(0x1140-16, 0);
  PanicCapture::capture(record, 0x40300003, 0x1100, 0x82001003, low, high, read, true);
  assert(record.magic == PanicCapture::kMagic && record.depth == 3 && record.rows == 16);
  assert(record.frames[0].pc == 0x40300000 && record.frames[1].pc == 0x42001000);
  assert(record.frames[2].pc == 0x42002000 && record.frames[2].sp == 0x1180);
  assert(record.stopped == 0);
  for (uint32_t sp : {0U, 0xfffffff0U, 0x2000U, 0x1101U}) {
    reads = 0;
    PanicCapture::capture(record, 0x40300003, sp, 0x82001003, low, high, read, true);
    assert(reads == 0 && record.rows == 0 && record.stopped == 1);
  }
  put(0x1100-12, 0x1100); // self-referential/corrupt frame
  PanicCapture::capture(record, 0x40300003, 0x1100, 0x82001003, low, high, read, true);
  assert(record.depth == 1 && record.stopped == 1);
  PanicCapture::capture(record, 0x40300003, high-16, 0, low, high, read, true);
  assert(record.rows == 0 && record.depth == 1); // no read past upper DRAM limit
  for (uint32_t sp=0x1100; sp<0x1500; sp+=16) {
    put(sp-12, sp+16); put(sp-16, 0x82001003);
  }
  PanicCapture::capture(record, 0x40300003, 0x1100, 0x82001003, low, high, read, true);
  assert(record.depth == PanicCapture::kDepth && record.stopped == 2);
  PanicCapture::capture(record, 0x40300003, 0x1100, 0, low, high, read, false);
  assert(record.depth == 0 && record.rows == 16); // RISC-V raw-stack compatibility
  puts("Panic capture: valid chain, corrupt/unreadable stacks, limits and raw capture passed");
}
