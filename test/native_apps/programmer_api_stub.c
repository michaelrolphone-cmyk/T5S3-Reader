#include "T5ProgramEspRomApi.h"
#include "T5DeviceApi.h"
#include "T5LocationApi.h"
#include "T5PackageManagerApi.h"
#include <stdbool.h>

/* The launcher test links an isolated host symbol table, not the firmware. */
const t5_program_esp_rom_api_v1 *t5_program_esp_rom_get_api(uint32_t version) {
    (void)version;
    return 0;
}
const t5_device_api_v1 *t5_device_get_api(uint32_t version) {
    (void)version;
    return 0;
}
const t5_location_api_v1 *t5_location_get_api(uint32_t version) {
    (void)version;
    return 0;
}
const t5_package_manager_api_v1 *t5_package_manager_get_api(uint32_t version) {
    (void)version;
    return 0;
}

int test_capability_gate_allowed = 1;
int test_capability_bind_allowed = 1;
int test_capability_bind_calls = 0;
int test_capability_active = 0;
int test_capability_release_calls = 0;
bool native_app_capabilities_ready(const char *sd_path) {
    (void)sd_path;
    return test_capability_gate_allowed != 0;
}
bool native_app_capabilities_bind(const char *sd_path) {
    (void)sd_path;
    ++test_capability_bind_calls;
    if (!test_capability_bind_allowed) return false;
    test_capability_active = 1;
    return true;
}
void native_app_capabilities_release(void) {
    if (test_capability_active) {
        test_capability_active = 0;
        ++test_capability_release_calls;
    }
}