#include "T5ProgramEspRomApi.h"

/* The launcher test links an isolated host symbol table, not the firmware. */
const t5_program_esp_rom_api_v1 *t5_program_esp_rom_get_api(uint32_t version) {
    (void)version;
    return 0;
}
