/* Actual RTC provider + actual HalClock + actual owner/lease adapter. */
#include "native/NativeRtcClock.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include <HalClock.h>
#include <Logging.h>
#include <RiscI2cBusV1.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <thread>

extern "C" const risc_driver_v2* t5_driver_get(uint32_t);
thread_local bool rtcTestCritical = false;
static thread_local unsigned task = 1;
TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void*>(static_cast<uintptr_t>(task)); }
static const risc_driver_v2* driver;
static uint8_t regs[16];
static bool busHeld, releaseOk = true, readOk = true, writeOk = true, present = true, partial = false;
static unsigned acquisitions, releases, reads, writes, claims;
static int failedReadRegister = -1;
static uint32_t generation;
static RuntimeInstalledProviders::Lease outstanding;
static time_t epoch;
static unsigned sets;
extern "C" time_t time(time_t* out) noexcept { if (out) *out = epoch; return epoch; }
extern "C" int settimeofday(const timeval* tv, const struct timezone*) noexcept {
    ++sets; epoch = tv->tv_sec; return 0;
}
static void callbackCheck() {
    assert(task == 1 && !rtcTestCritical);
    risc_rtc_time_v2 unused{};
    assert(!nativeRtcRead(&unused)); // Reentrant callback must not touch graph.
}
static bool claimDevice(void*, uint8_t address, uint64_t* out) {
    callbackCheck(); assert(address == 0x51 && !busHeld); ++claims;
    *out = 77; busHeld = true; return true;
}
static bool transact(void*, uint64_t token, const uint8_t* w, size_t wn,
                     uint8_t* r, size_t rn, uint32_t timeout) {
    callbackCheck(); assert(busHeld && token == 77 && w && wn && timeout == 30);
    assert(w[0] + (rn ? rn : wn - 1) <= sizeof(regs));
    if (rn) {
        ++reads; assert(wn == 1); if (!readOk || w[0] == failedReadRegister) return false;
        memcpy(r, regs + w[0], rn);
    } else {
        ++writes; assert(wn > 1); if (!writeOk) return false;
        memcpy(regs + w[0], w + 1, wn - 1);
    }
    return true;
}
static bool releaseDevice(void*, uint64_t token) {
    callbackCheck(); assert(busHeld && token == 77);
    if (!releaseOk) return false;
    busHeld = false; return true;
}
static risc_i2c_bus_contract_v1 bus = {
    {1, sizeof(bus), nullptr, claimDevice, transact, releaseDevice},
    RISC_I2C_BUS_CONTRACT_TAG, RISC_I2C_BUS_CONTRACT_V1, RISC_I2C_BUS_SAFE_CONTRACT_FLAGS
};
namespace RuntimeInstalledProviders {
bool acquireCapability(const char* capability, uint32_t api, Lease* out) {
    callbackCheck(); ++acquisitions;
    assert(!strcmp(capability, "rtc.clock") && api == 2 && !out->grant.slot && !outstanding.grant.slot);
    if (!present && !partial) return false;
    risc_provider_dependency_v1 deps{"i2c.bus", 1, &bus.base};
    if (!driver->start(&deps, 1)) return false;
    *out = {{1, ++generation}, partial ? nullptr : driver->capability};
    outstanding = *out; return !partial;
}
bool release(Lease* out) {
    callbackCheck(); ++releases;
    assert(out->grant.slot == outstanding.grant.slot && out->grant.generation == outstanding.grant.generation);
    if (!driver->quiesce()) { out->interface = nullptr; return false; }
    *out = {}; outstanding = {}; return true;
}
bool copyProviderError(const Lease& value, char* out, size_t capacity) {
    callbackCheck();
    assert(value.interface && value.grant.slot == outstanding.grant.slot &&
           value.grant.generation == outstanding.grant.generation && busHeld);
    const auto* diagnostic = reinterpret_cast<const risc_driver_diagnostics_v2*>(driver);
    const unsigned beforeReads = reads, beforeWrites = writes;
    const bool copied = diagnostic->last_error(out, capacity);
    assert(reads == beforeReads && writes == beforeWrites);
    return copied;
}
const char* lastError() { return "fixture RTC unavailable"; }
}
static void valid() {
    // 2026-10-04 Sunday 12:34:56, UTC by selected test convention.
    const uint8_t v[] = {0,0,0x56,0x34,0x12,0x04,0,0x10,0x26};
    memcpy(regs, v, sizeof(v));
}
static void configure(bool utc = true) {
    halClock.begin(); halClock.configure("America/Denver", utc, 1, 0);
}
static void cold() {
    configure(); assert(!epoch && !writes);
    assert(halClock.syncSystemTimeFromRtc());
    assert(epoch == 1791117296 && sets == 1 && !writes && !busHeld);
    uint8_t hour = 0, minute = 0; assert(halClock.getTime(hour, minute));
    assert(hour == 6 && minute == 34); // Existing timezone conversion retained.
    const unsigned oldAcquisitions = acquisitions;
    epoch += 60;
    assert(halClock.getTime(hour, minute) && minute == 35);
    assert(acquisitions == oldAcquisitions); // Minute wake consumes retained SDK time.
}
static void local() {
    configure(false); assert(halClock.syncSystemTimeFromRtc());
    assert(epoch == 1791138896 && !writes);
    uint8_t hour, minute; assert(halClock.getTime(hour, minute) && hour == 12 && minute == 34);
}
static void invalid() {
    configure();
    const struct {unsigned index; uint8_t value;} faults[] = {
        {2,0xd6}, {3,0x6a}, {4,0x24}, {5,0}, {6,7}, {7,0x90}, {7,0}, {8,0xaa}, {0,0x20}, {0,0x80}, {0,8}
    };
    for (const auto fault : faults) {
        valid(); regs[fault.index] = fault.value;
        assert(!halClock.syncSystemTimeFromRtc() && !epoch && !sets && !writes && !busHeld);
    }
    assert(nativeDiagnosticContains("RTC read rejected: rtc voltage-low: time invalid"));
    assert(nativeDiagnosticContains("RTC read rejected: rtc stopped/test mode"));
    assert(nativeDiagnosticContains("RTC read rejected: rtc invalid BCD"));
    assert(nativeDiagnosticContains("RTC read rejected: rtc invalid calendar"));
    valid(); regs[7] = 2; regs[5] = 0x29; // 2026 is not leap year.
    assert(!halClock.syncSystemTimeFromRtc());
    regs[8] = 0x24; assert(halClock.syncSystemTimeFromRtc());
    failedReadRegister = 2;
    assert(!halClock.syncSystemTimeFromRtc());
    assert(nativeDiagnosticContains("RTC read rejected: rtc read I/O"));
    failedReadRegister = -1;
    readOk = false; assert(!halClock.syncSystemTimeFromRtc());
    // Admission reads fail before the time read and report an unavailable
    // capability; a live provider read fault retains its distinct diagnostic.
    assert(nativeDiagnosticContains("RTC capability unavailable"));
}
static void writeTime() {
    configure();
    assert(!halClock.syncRtcFromSystemTime() && !writes); // Unset SDK cannot overwrite RTC.
    epoch = 1791117296;
    for (const uint8_t control : {uint8_t{0x20}, uint8_t{0x77}}) {
        // Reserved N bits may read 1 with STOP, but every N bit must write 0.
        regs[2] |= 0x80; regs[0] = control;
        const unsigned before = writes;
        assert(halClock.syncRtcFromSystemTime() && writes == before + 2 && regs[0] == 0 && regs[2] == 0x56);
    }
    assert(halClock.getVariantHint() == 2); // Never trust alternate T5 layout hint on X4.
    writeOk = false; assert(!halClock.syncRtcFromSystemTime());
    assert(!busHeld);
}
static void retained() {
    configure(); releaseOk = false;
    assert(!halClock.syncSystemTimeFromRtc() && !epoch && !sets && busHeld);
    const unsigned old = acquisitions;
    assert(!nativeRtcSuspend() && !nativeRtcResume());
    risc_rtc_time_v2 value{}; assert(!nativeRtcRead(&value) && acquisitions == old);
    releaseOk = true; assert(nativeRtcSuspend() && !busHeld);
    assert(!nativeRtcRead(&value) && acquisitions == old);
    assert(nativeRtcResume() && nativeRtcRead(&value));
    assert(!busHeld);
}
static void missing() {
    present = false; configure(); assert(!halClock.isAvailable());
    assert(!halClock.syncSystemTimeFromRtc() && !epoch && !sets && !writes);
    assert(nativeRtcSuspend() && nativeRtcResume());
}
static void threads() {
    configure(); const unsigned old = acquisitions;
    std::thread worker([] {
        task = 2; risc_rtc_time_v2 value{2026,10,4,0,12,34,56};
        assert(!nativeRtcRead(&value) && !nativeRtcWrite(&value));
        assert(!nativeRtcBegin() && !nativeRtcSuspend() && !nativeRtcResume());
    }); worker.join(); assert(acquisitions == old);
    assert(halClock.syncSystemTimeFromRtc());
}
static void fastWake() {
    // Fast path skips begin entirely. Trusted sleep owner can still drain.
    assert(nativeRtcSuspend()); risc_rtc_time_v2 value{};
    assert(!nativeRtcRead(&value) && acquisitions == 0);
    assert(nativeRtcResume() && nativeRtcRead(&value));
}
static void partialGrant() {
    partial = true; releaseOk = false;
    assert(!nativeRtcBegin() && busHeld);
    const unsigned old = acquisitions;
    assert(!nativeRtcSuspend());
    releaseOk = true; assert(nativeRtcSuspend());
    assert(acquisitions == old && !busHeld);
    partial = false; assert(nativeRtcResume());
    risc_rtc_time_v2 value{}; assert(nativeRtcRead(&value));
}
static void contracts() {
    const risc_provider_dependency_v1 dep{"i2c.bus", 1, &bus.base};
    assert(!driver->start(nullptr, 0) && !driver->start(&dep, 0) && !driver->start(&dep, 2));
    const auto saved = bus;
    bus.base.api_version = 2; assert(!driver->start(&dep, 1)); bus = saved;
    bus.base.struct_size = sizeof(bus.base); assert(!driver->start(&dep, 1)); bus = saved;
    bus.contract_tag = 0; assert(!driver->start(&dep, 1)); bus = saved;
    bus.contract_version = 0; assert(!driver->start(&dep, 1)); bus = saved;
    bus.contract_flags &= ~RISC_I2C_BUS_RETAINED_RELEASE;
    assert(!driver->start(&dep, 1)); bus = saved;
    bus.base.transact = nullptr; assert(!driver->start(&dep, 1)); bus = saved;
    assert(!claims && !reads && !writes);
    readOk = false; releaseOk = false;
    assert(!driver->start(&dep, 1) && busHeld); // Failed start cleanup stays owned.
    assert(!driver->start(&dep, 1) && !driver->quiesce());
    char error[64]{};
    auto* diag = reinterpret_cast<const risc_driver_diagnostics_v2*>(driver);
    assert(diag->last_error(error, sizeof(error)) && !strcmp(error, "rtc release pending"));
    releaseOk = true; readOk = true; assert(driver->quiesce() && !busHeld);
    assert(driver->start(&dep, 1));
    auto* api = static_cast<const risc_rtc_clock_api_v2*>(driver->capability);
    const unsigned oldWrites = writes;
    risc_rtc_time_v2 value{2100,1,1,5,0,0,0};
    assert(!api->write(nullptr, &value) && !api->read(nullptr, nullptr));
    assert(writes == oldWrites);
    const unsigned oldReads = reads;
    assert(!api->alarm(nullptr, 1, 2, 3, 4, true));
    assert(writes == oldWrites && reads == oldReads); // No unverified IRQ write.
    regs[1] = 0xff; // All read-only N bits set, plus each defined control flag.
    assert(api->alarm(nullptr, 255, 255, 255, 255, false));
    assert(regs[1] == 0x15); // Clear AF/AIE/N, preserve TI_TP/TF/TIE.
    for (unsigned i = 9; i < 13; ++i) assert(regs[i] == 0x80);
    regs[1] = 0xff;
    bool pending = false;
    const unsigned beforeAck = writes;
    assert(api->alarm_pending(nullptr, &pending, false) && pending);
    assert(writes == beforeAck && regs[1] == 0xff); // Pure observation.
    assert(api->alarm_pending(nullptr, &pending, true) && pending);
    assert(regs[1] == 0x17); // Clear AF/N; retain TI_TP/TF/AIE/TIE.
    assert(driver->quiesce());
}
int main(int argc, char** argv) {
    assert(argc == 2); driver = t5_driver_get(2); assert(driver); valid();
    if (!strcmp(argv[1],"cold")) cold();
    else if (!strcmp(argv[1],"local")) local();
    else if (!strcmp(argv[1],"invalid")) invalid();
    else if (!strcmp(argv[1],"write")) writeTime();
    else if (!strcmp(argv[1],"retained")) retained();
    else if (!strcmp(argv[1],"missing")) missing();
    else if (!strcmp(argv[1],"threads")) threads();
    else if (!strcmp(argv[1],"fastwake")) fastWake();
    else if (!strcmp(argv[1],"partial")) partialGrant();
    else if (!strcmp(argv[1],"contracts")) contracts();
    else assert(false);
    printf("RTC provider/HAL %s: PASS\n", argv[1]);
}
