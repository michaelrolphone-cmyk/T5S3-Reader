#pragma once
#include <cstdint>
#include "esp_err.h"

// Isolated module-store bootstrap. Does not publish a runtime capability.
// Every filesystem operation must run on the mounting task inside an explicit
// budget. The caller must close all FAT handles before releasing the controller.
namespace BootstrapSdmmc {
struct Pins { int clk; int cmd; int d0; };
struct Progress { uint32_t sectors; uint32_t commands; esp_err_t error; };
esp_err_t mount(Pins pins, bool writable = false);
esp_err_t unmount();
bool beginOperation(uint32_t milliseconds, uint32_t maxSectors);
Progress endOperation();
const char* drive();
void cancel();
// Includes failed mount/deinit ownership; absence of a mounted volume is not release.
bool retained();
}
