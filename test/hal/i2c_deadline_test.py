#!/usr/bin/env python3
"""Run the actual private Wire transport with deterministic lock/time faults."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
board = r'''
#pragma once
#include <cstdint>
#include <cassert>
extern uint32_t nowMs, lockDelay, lockBudget;
extern bool grantLock;
inline uint32_t millis() { return nowMs; }
namespace BoardT5S3 {
class ScopedI2CLock {
  bool locked;
public:
  explicit ScopedI2CLock(uint32_t timeout = UINT32_MAX) {
    lockBudget=timeout; nowMs+=lockDelay; locked=grantLock;
  }
  bool acquired() const { return locked; }
};
}
'''
wire = r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
struct FakeWire {
  unsigned calls=0, rx=0;
  uint32_t timeout=50, usedTimeout=0;
  bool fail=false;
  void setTimeOut(uint32_t ms) { timeout=ms; }
  void beginTransmission(uint8_t) { ++calls; }
  size_t write(const uint8_t*, size_t n) { return n; }
  unsigned endTransmission(bool) { usedTimeout=timeout; return fail ? 1 : 0; }
  uint8_t requestFrom(uint8_t, uint8_t n) {
    ++calls; usedTimeout=timeout; rx=n; return n;
  }
  int read() { assert(rx); --rx; return 0x42; }
  int available() { return rx; }
};
extern FakeWire Wire;
'''
fixture = r'''
#include "BoardT5S3.h"
#include "Wire.h"
#include "RiscFirmwareI2cCompatV1.h"
#include <cstdio>
uint32_t nowMs=100, lockDelay=0, lockBudget=0;
bool grantLock=true;
FakeWire Wire;
int main() {
  uint8_t reg=1, out=0;
  grantLock=false; lockDelay=20;
  assert(!risc_fw_i2c_transact_v1(0x5d,&reg,1,&out,1,20));
  assert(lockBudget==20 && Wire.calls==0);
  grantLock=true; lockDelay=20;
  assert(!risc_fw_i2c_transact_v1(0x5d,&reg,1,&out,1,20));
  assert(Wire.calls==0); // no transfer after admission budget expires
  lockDelay=7;
  assert(risc_fw_i2c_transact_v1(0x5d,&reg,1,&out,1,20));
  assert(Wire.usedTimeout==13 && Wire.timeout==50 && out==0x42);
  Wire.fail=true;
  assert(!risc_fw_i2c_transact_v1(0x5d,&reg,1,&out,1,20));
  assert(Wire.timeout==50);
  Wire.fail=false; nowMs=UINT32_MAX-3; lockDelay=7;
  assert(risc_fw_i2c_transact_v1(0x5d,&reg,1,&out,1,20));
  assert(Wire.usedTimeout==13);
  puts("private I2C transport: lock timeout, remaining budget, restore, rollover PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp/'BoardT5S3.h').write_text(board)
    (temp/'Wire.h').write_text(wire)
    (temp/'fixture.cpp').write_text(fixture)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                    '-DBOARD_T5S3_PRO','-I'+str(temp),'-I'+str(ROOT/'sdk/driver'),
                    str(ROOT/'src/native/FirmwareI2cCompat.cpp'),str(temp/'fixture.cpp'),
                    '-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
