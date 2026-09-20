// Temporary direct-import compatibility for full native hardware ELFs.
// This is not a driver implementation and does not change capability leasing.
// It binds ONLY the exact ABI names listed in NativeHardwareCompatSymbols.def.
#include "esp_elf.h"
#include <errno.h>

#if defined(BOARD_T5S3_PRO)
// Make the Arduino libraries actual firmware link dependencies: relying only
// on assembly symbol aliases would leave PlatformIO's library discovery blind.
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_io_i80.h>
#include <esp_heap_caps.h>

// Assembly aliases bind the *existing* implementation by its exact linker
// name, without reimplementing a driver or declaring incorrect C++ member
// prototypes. These are used only to obtain symbol addresses, never called.
// Function-typed aliases can also refer to global data: the symbol table
// stores addresses and does not invoke them.
#define RISC_COMPAT_SYMBOL(name) \
    extern "C" void riscrte_compat_##name(void) __asm__(#name);
#include "NativeHardwareCompatSymbols.def"
#undef RISC_COMPAT_SYMBOL

static const struct esp_elfsym native_hardware_compat_symbols[] = {
#define RISC_COMPAT_SYMBOL(name) \
    { #name, reinterpret_cast<const void *>(&riscrte_compat_##name) },
#include "NativeHardwareCompatSymbols.def"
#undef RISC_COMPAT_SYMBOL
    ESP_ELFSYM_END
};

extern "C" int native_hardware_compat_register(void)
{
    return esp_elf_register_symbol(native_hardware_compat_symbols);
}

extern "C" void native_hardware_compat_unregister(void)
{
    (void)esp_elf_unregister_symbol(native_hardware_compat_symbols);
}
#else
// No ESP32-S3/T5S3 direct hardware ABI exists on other board variants.
extern "C" int native_hardware_compat_register(void) { return 0; }
extern "C" void native_hardware_compat_unregister(void) {}
#endif
