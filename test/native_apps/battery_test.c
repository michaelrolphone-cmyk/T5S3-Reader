#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5BatteryApi.h"
#include "T5UiApi.h"

void app_main(void);

static int polls;
static int reads;
static int renders;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    if (polls == 1) input->buttons = T5_APP_BUTTON_CONFIRM;
    else if (polls == 2) input->buttons = T5_APP_BUTTON_DOWN;
    else input->buttons = T5_APP_BUTTON_BACK;
    return true;
}

static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = app_poll,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

static bool battery_read(t5_battery_state_t *state) {
    assert(state);
    memset(state, 0, sizeof(*state));
    ++reads;
    strcpy(state->board_name, "T5S3 Pro");
    state->available = 1;
    state->detailed_telemetry = 1;
    state->charger_ready = 1;
    state->gauge_ready = 1;
    state->charger_read_ok = 1;
    state->gauge_read_ok = 1;
    state->vbus_connected = 1;
    state->charge_enabled = 1;
    state->charging = 1;
    state->gauge_state = T5_BATTERY_GAUGE_CHARGE;
    state->charger_status = T5_BATTERY_CHARGE_FAST;
    state->soc_percent = 73;
    state->soh_percent = 96;
    state->gauge_voltage_mv = 3912;
    state->battery_voltage_mv = 3907;
    state->vbus_voltage_mv = 5080;
    state->system_voltage_mv = 4010;
    state->charge_voltage_mv = 4208;
    state->charge_current_ma = 750;
    state->precharge_current_ma = 128;
    state->termination_current_ma = 128;
    state->charger_adc_current_ma = 716;
    state->average_current_ma = 412;
    state->current_ma = 430;
    state->profile_capacity_mah = 2100;
    state->full_capacity_mah = 2040;
    state->remaining_capacity_mah = 1490;
    state->temperature_dk = 2981;
    state->gauge_charge_voltage_mv = 4200;
    state->gauge_taper_current_ma = 128;
    return true;
}

static const t5_battery_api_v1 battery_api = {
    .api_version = T5_BATTERY_API_VERSION,
    .struct_size = sizeof(t5_battery_api_v1),
    .read = battery_read,
};

const t5_battery_api_v1 *t5_battery_get_api(uint32_t version) {
    return version == T5_BATTERY_API_VERSION ? &battery_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows);
    assert(strcmp(chrome->title, "Battery Status") == 0);
    assert(strcmp(chrome->confirm_label, "Update") == 0);
    assert(row_count == 28);
    assert(strcmp(rows[0].title, "Mode") == 0);
    assert(strcmp(rows[0].value, "Charge") == 0);
    assert(strcmp(rows[1].value, "73 %") == 0);
    assert(strcmp(rows[2].value, "3912 mV") == 0);
    assert(strcmp(rows[11].title, "BQ27220") == 0);
    assert(strcmp(rows[19].title, "BQ25896") == 0);
    assert(strcmp(rows[26].title, "Charger ADC I") == 0);
    assert(strcmp(rows[27].value, "Fast charge / Enabled") == 0);
    if (renders < 2) assert(selected_index == 0);
    else assert(selected_index == 1);
    ++renders;
}

static int32_t hit_test(int16_t x, int16_t y) {
    (void)x;
    (void)y;
    return T5_UI_HIT_NONE;
}

static int32_t next_index(int32_t current, uint32_t count) {
    assert(count == 28);
    return (current + 1) % (int32_t)count;
}

static int32_t previous_index(int32_t current, uint32_t count) {
    assert(count == 28);
    return current <= 0 ? (int32_t)count - 1 : current - 1;
}

static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
    .next_index = next_index,
    .previous_index = previous_index,
};

const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    app_main();
    assert(polls == 3);
    assert(reads == 2);
    assert(renders == 3);
    return 0;
}
