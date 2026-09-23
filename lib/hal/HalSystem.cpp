#include "HalSystem.h"
#include "PanicCapture.h"
#include "soc/soc.h"
#include "esp_ota_ops.h"

#include <string>

#include "Arduino.h"
#include "HalStorage.h"
#include "Logging.h"
#include "esp_debug_helpers.h"
#include "esp_attr.h"
#include "../../src/DeskClockSleep.h"
#if CONFIG_IDF_TARGET_ESP32C3
#include "esp_private/esp_cpu_internal.h"
#else
#include "freertos/xtensa_context.h"
#endif
#include "esp_private/panic_internal.h"

RTC_NOINIT_ATTR char panicMessage[256];
RTC_NOINIT_ATTR volatile PanicCapture::Record panicRecords[2];

extern "C" {

void __real_panic_abort(const char* message);
void __real_panic_print_backtrace(const void* frame, int core);

static DRAM_ATTR const char PANIC_REASON_UNKNOWN[] = "(unknown panic reason)";
void IRAM_ATTR __wrap_panic_abort(const char* message) {
  if (!message) message = PANIC_REASON_UNKNOWN;
  int i = 0;
  for (; i < (int)sizeof(panicMessage) - 1 && message[i]; i++) {
    panicMessage[i] = message[i];
  }
  panicMessage[i] = '\0';
  __real_panic_abort(message);
}

void IRAM_ATTR __wrap_panic_print_backtrace(const void* frame, int core) {
  if (!frame) {
    __real_panic_print_backtrace(frame, core);
    return;
  }
  if (core >= 0 && core < 2) {
    auto &record = panicRecords[core];
#if CONFIG_IDF_TARGET_ESP32C3
    const auto *f = static_cast<const RvExcFrame *>(frame);
    const uint32_t pc = f->mepc, sp = f->sp, a0 = f->ra;
    record.ps = f->mstatus; record.cause = f->mcause; record.address = f->mtval;
    constexpr bool xtensa = false;
#else
    const auto *f = static_cast<const XtExcFrame *>(frame);
    const uint32_t pc = f->pc, sp = f->a1, a0 = f->a0;
    record.ps = f->ps; record.cause = f->exccause; record.address = f->excvaddr;
    constexpr bool xtensa = true;
#endif
    // Restrict reads to internal DRAM: external stacks may be inaccessible
    // when a cache/flash fault is what brought us into the panic handler.
    PanicCapture::capture(record, pc, sp, a0, SOC_DRAM_LOW, SOC_DRAM_HIGH,
                          [](uint32_t address) __attribute__((always_inline)) {
                            return *reinterpret_cast<const volatile uint32_t *>(address);
                          }, xtensa);
  }
  __real_panic_print_backtrace(frame, core);
}
}

namespace HalSystem {

void begin() {
  // Deep sleep restarts the MCU. Handle a retained minute-timer wake before
  // SD mounting, touch initialization, app loading or activity/render tasks.
  // A button wake clears clock mode and continues through normal setup().
  if (DeskClockSleep::resumeAfterTimerWake()) return;

  // Keep normal panic and log initialization unchanged for regular boots.
  if (!isRebootFromPanic()) {
    clearPanic();
  } else {
    if (sanitizeLogHead()) {
      clearLastLogs();
    }
    preserveLastLogs(true);
  }
  installSdkLogCapture();
}

void checkPanic() {
  if (isRebootFromPanic()) {
    auto panicInfo = getPanicInfo(true);
    auto file = Storage.open("/crash_report.txt", O_WRITE | O_CREAT | O_TRUNC);
    if (file) {
      file.write(panicInfo.c_str(), panicInfo.size());
      file.close();
      LOG_INF("SYS", "Dumped panic info to SD card");
    } else {
      LOG_ERR("SYS", "Failed to open crash_report.txt for writing");
    }
  }
}

void clearPanic() {
  panicMessage[0] = '\0';
  for (auto &record : panicRecords) record.magic = 0;
  preserveLastLogs(false);
  clearLastLogs();
}

std::string getPanicInfo(bool full) {
  if (!full) {
    panicMessage[sizeof(panicMessage) - 1] = '\0';
    return panicMessage;
  } else {
    std::string info;
    panicMessage[sizeof(panicMessage) - 1] = '\0';
    info += "RiscRTE version: " CROSSPOINT_VERSION;
    char sha[65] = {};
    esp_ota_get_app_elf_sha256(sha, sizeof(sha));
    info += "\nFirmware ELF SHA256: " + std::string(sha);
    info += "\n\nPanic reason: " + std::string(panicMessage);
    info += "\n\nLast logs:\n" + getLastLogs();

    auto toHex = [](uint32_t value) {
      char buffer[9];
      snprintf(buffer, sizeof(buffer), "%08X", value);
      return std::string(buffer);
    };
    bool captured = false;
    for (size_t core = 0; core < 2; ++core) {
      const auto &record = panicRecords[core];
      if (record.magic != PanicCapture::kMagic) continue;
      captured = true;
      info += "\n\nCore " + std::to_string(core) + " saved exception frame:";
      info += "\nPC=0x" + toHex(record.pc) + " SP=0x" + toHex(record.sp);
      info += " A0/RA=0x" + toHex(record.a0) + " PS/STATUS=0x" + toHex(record.ps);
      info += "\nCAUSE=0x" + toHex(record.cause) + " FAULT_ADDR=0x" + toHex(record.address);
      info += "\nBacktrace:";
      for (size_t i = 0; i < record.depth && i < PanicCapture::kDepth; ++i)
        info += " 0x" + toHex(record.frames[i].pc) + ":0x" + toHex(record.frames[i].sp);
      if (record.stopped == 1) info += " [stopped: unsafe/corrupt stack]";
      if (record.stopped == 2) info += " [depth limit]";
      info += "\nStack memory (internal DRAM only):\n";
      if (!record.rows) info += "Unavailable: stack pointer outside safe internal DRAM.\n";
      for (size_t i = 0; i < record.rows && i < PanicCapture::kRows; ++i) {
        info += "0x" + toHex(record.stack[i].sp) + ": ";
        for (size_t j = 0; j < 8; ++j) info += "0x" + toHex(record.stack[i].words[j]) + " ";
        info += "\n";
      }
    }
    if (!captured) info += "\nNo retained exception frame was captured.\n";
    return info;
  }
}

bool isRebootFromPanic() {
  const auto resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC) return true;
#ifdef ESP_RST_CPU_LOCKUP
  if (resetReason == ESP_RST_CPU_LOCKUP) return true;
#endif
  return false;
}

}  // namespace HalSystem
