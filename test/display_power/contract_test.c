#include <assert.h>
#include <stddef.h>
#include "RiscDisplayOutputPowerV1.h"

static bool seed(void *c, risc_display_frame_v1 f) { (void)c; (void)f; return true; }
static int32_t power(void *c, uint32_t ms) { (void)c; (void)ms; return RISC_DISPLAY_POWER_OK; }
int main(void) {
    risc_display_output_api_v1_power api = {0};
    api.history.base.api_version = RISC_DISPLAY_OUTPUT_API_V1;
    api.history.base.struct_size = sizeof(api);
    api.history.extension_tag = RISC_DISPLAY_HISTORY_TAG;
    api.history.extension_version = 1;
    api.history.seed_previous = seed;
    api.power_tag = RISC_DISPLAY_POWER_TAG; api.power_version = 1;
    api.prepare = power; api.resume = power;
    assert(offsetof(risc_display_output_api_v1_power, history) == 0);
    assert(offsetof(risc_display_output_api_v1_power, power_tag) == sizeof(risc_display_output_api_v1_history));
    assert(!risc_display_output_power(NULL));
    assert(risc_display_output_history(&api.history.base) == &api.history);
    assert(risc_display_output_power(&api.history.base) == &api);
    assert(api.prepare(api.history.base.context, 0) == RISC_DISPLAY_POWER_OK);
    api.history.base.struct_size = sizeof(risc_display_output_api_v1_history);
    assert(risc_display_output_history(&api.history.base) && !risc_display_output_power(&api.history.base));
    api.history.base.struct_size = sizeof(risc_display_output_api_v1);
    assert(!risc_display_output_history(&api.history.base) && !risc_display_output_power(&api.history.base));
    api.history.base.struct_size = sizeof(api) - 1;
    assert(!risc_display_output_power(&api.history.base));
    api.history.base.struct_size = sizeof(api);
    api.power_tag ^= 1; assert(!risc_display_output_power(&api.history.base)); api.power_tag ^= 1;
    api.power_version = 2; assert(!risc_display_output_power(&api.history.base)); api.power_version = 1;
    api.resume = NULL; assert(!risc_display_output_power(&api.history.base)); api.resume = power;
    api.prepare = NULL; assert(!risc_display_output_power(&api.history.base)); api.prepare = power;
    api.history.seed_previous = NULL; assert(!risc_display_output_power(&api.history.base));
    return 0;
}
