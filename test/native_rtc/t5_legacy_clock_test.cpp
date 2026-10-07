#include <HalClock.h>
#include <Wire.h>
#include <sys/time.h>
#include <cassert>
#include <cstdio>
#include <cstring>
TestWire Wire;
static time_t epoch;
extern "C" time_t time(time_t* out) noexcept { if (out) *out = epoch; return epoch; }
extern "C" int settimeofday(const timeval* tv, const struct timezone*) noexcept { epoch = tv->tv_sec; return 0; }
void TestWire::beginTransmission(uint8_t address) { assert(address == 0x51); length = 0; }
size_t TestWire::write(uint8_t byte) { assert(length < sizeof(tx)); tx[length++] = byte; return 1; }
size_t TestWire::write(const uint8_t* data, size_t n) { for (size_t i = 0; i < n; ++i) write(data[i]); return n; }
uint8_t TestWire::endTransmission(bool) {
    assert(length); if (!ok) return 1; cursor = tx[0];
    if (length > 1) { ++writes; memcpy(registers + cursor, tx + 1, length - 1); }
    return 0;
}
uint8_t TestWire::requestFrom(uint8_t address, uint8_t count) { assert(address == 0x51); remaining = ok ? count : 0; return remaining; }
int TestWire::available() { return remaining; }
int TestWire::read() { assert(remaining); --remaining; return registers[cursor++]; }
int main(int argc, char** argv) {
    assert(argc == 2);
    const bool pcf85063 = !strcmp(argv[1], "85063");
    const uint8_t start = pcf85063 ? 4 : 2;
    const uint8_t valid[] = {0x56, 0x34, 0x12, 0x04, 0, 0x10, 0x26};
    memcpy(Wire.registers + start, valid, 7);
    halClock.begin(); halClock.configure("America/Denver", true, pcf85063 ? 1 : 2);
    assert(halClock.isAvailable() && halClock.syncSystemTimeFromRtc());
    assert(epoch == 1791117296 && !Wire.writes);
    assert(halClock.syncRtcFromSystemTime() && Wire.writes == 1);
    assert(halClock.getVariantHint() == (pcf85063 ? 1 : 2));
    Wire.registers[start] |= 0x80; epoch = 0;
    assert(!halClock.syncSystemTimeFromRtc() && !epoch);
    Wire.ok = false; assert(!halClock.syncSystemTimeFromRtc());
    printf("Legacy T5 PCF%s clock: PASS\n", argv[1]);
}
