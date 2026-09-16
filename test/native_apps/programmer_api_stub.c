#include "T5ProgramEspRomApi.h"
#include "T5DeviceApi.h"
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

int test_capability_gate_allowed = 1;
bool native_app_capabilities_ready(const char *sd_path) {
    (void)sd_path;
    return test_capability_gate_allowed != 0;
}
