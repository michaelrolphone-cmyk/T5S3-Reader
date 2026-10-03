// Temporary direct-import compatibility for full native hardware ELFs.
// This is not a driver implementation and does not change capability leasing.
// It binds ONLY the exact ABI names listed in NativeHardwareCompatSymbols.def.
#include "esp_elf.h"
#include <errno.h>
#include <cstring>
#include <Logging.h>

namespace {
bool retiredStorageImport = false;
bool retiredDisplayImport = false;
}
extern "C" bool native_app_import_allowed(const char* name) {
    if (!name) return false;
    static const char* const retired[] = {
#define RISC_RETIRED_STORAGE_IMPORT(symbol) #symbol,
#include "RetiredStorageImports.def"
#undef RISC_RETIRED_STORAGE_IMPORT
    };
    for (const auto* symbol : retired) {
        if (std::strcmp(name, symbol)) continue;
        if (!retiredStorageImport)
            LOG_ERR("APP", "Retired raw SD import %s: rebuild with t5_storage_get_api or /sd VFS", name);
        retiredStorageImport = true;
        return false;
    }
    static const char* const retiredDisplay[] = {
#define RISC_RETIRED_DISPLAY_IMPORT(symbol) #symbol,
#include "RetiredDisplayImports.def"
#undef RISC_RETIRED_DISPLAY_IMPORT
    };
    for (const auto* symbol : retiredDisplay) {
        if (std::strcmp(name, symbol)) continue;
        if (!retiredDisplayImport)
            LOG_ERR("APP", "Retired raw display import %s: rebuild with shared display capability", name);
        retiredDisplayImport = true;
        return false;
    }
    return true;
}
extern "C" const char* native_hardware_compat_last_error() {
    if (retiredDisplayImport) return "Legacy raw display app unsupported. Update it to use the shared display API.";
    return retiredStorageImport ? "Legacy raw SD app unsupported. Update it to use the shared storage API." : nullptr;
}
extern "C" void native_hardware_compat_clear_error() { retiredStorageImport = false; retiredDisplayImport = false; }

#if defined(BOARD_T5S3_PRO)
// Make the Arduino libraries actual firmware link dependencies: relying only
// on assembly symbol aliases would leave PlatformIO's library discovery blind.
#include <Arduino.h>
#include <HalStorage.h>
#include "NativeStorageImportPolicy.h"
#include <FS.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
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

// Registration/relocation/teardown use NativeAppLauncher's serialized owner
// task, as required by the existing global symbol-table API itself.
static bool compat_registered = false;
static bool raw_storage_imported = false;
extern "C" void esp_elf_registered_symbol_used(const void* table, const char* name, uintptr_t address) {
    if (address && compat_registered && table == native_hardware_compat_symbols &&
        !raw_storage_imported && nativeRawStorageImport(name)) {
        Storage.externalStorageBegin();
        raw_storage_imported = true;
    }
}
extern "C" int native_hardware_compat_register(void) {
    retiredStorageImport = false; retiredDisplayImport = false;
    const int result = esp_elf_register_symbol(native_hardware_compat_symbols);
    if (!result) { compat_registered = true; raw_storage_imported = false; }
    return result;
}
extern "C" void native_hardware_compat_storage_uncertain(void) {
    if (raw_storage_imported) Storage.externalStorageUncertain();
}
extern "C" void native_hardware_compat_unregister(void) {
    const int result = esp_elf_unregister_symbol(native_hardware_compat_symbols);
    if (raw_storage_imported) Storage.externalStorageEnd(result == 0);
    raw_storage_imported = false;
    if (!result) compat_registered = false;
}
#else
// No ESP32-S3/T5S3 direct hardware ABI exists on other board variants.
extern "C" int native_hardware_compat_register(void) { retiredStorageImport = false; return 0; }
extern "C" void native_hardware_compat_unregister(void) {}
extern "C" void native_hardware_compat_storage_uncertain(void) {}
#endif
