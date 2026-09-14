#include "T5AppApi.h"
#include "T5BatteryApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_ROWS 26u
#define VALUE_SIZE 72u

static const t5_app_api_v1 *app;
static const t5_battery_api_v1 *battery;
static const t5_ui_api_v1 *ui;
static t5_ui_list_row_t rows[MAX_ROWS];
static char values[MAX_ROWS][VALUE_SIZE];
static char footer[128];
static uint32_t row_count;
static int32_t selected_index;

static void copy_text(char *dst, size_t capacity, const char *src) {
    size_t i = 0;
    if (!dst || capacity == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static const char *yes_no(uint8_t value) { return value ? "Yes" : "No"; }

static const char *online_text(uint8_t ready, uint8_t read_ok) {
    if (!ready) return "Not found";
    return read_ok ? "Online" : "Read error";
}

static const char *charge_status_name(uint8_t status) {
    switch (status) {
        case T5_BATTERY_CHARGE_NOT_CHARGING: return "Not charging";
        case T5_BATTERY_CHARGE_PRECHARGE: return "Precharge";
        case T5_BATTERY_CHARGE_FAST: return "Fast charge";
        case T5_BATTERY_CHARGE_DONE: return "Done";
        default: return "Unknown";
    }
}

static const char *gauge_state_name(uint8_t state) {
    switch (state) {
        case T5_BATTERY_GAUGE_SLEEP: return "Sleep";
        case T5_BATTERY_GAUGE_FULL: return "Full";
        case T5_BATTERY_GAUGE_CHARGE: return "Charge";
        case T5_BATTERY_GAUGE_DISCHARGE: return "Discharge";
        case T5_BATTERY_GAUGE_RELAX: return "Relax";
        default: return "Unknown";
    }
}

static const char *mode_name(const t5_battery_state_t *state) {
    if (!state->available) return "Unavailable";
    if (state->gauge_ready && state->gauge_read_ok) return gauge_state_name(state->gauge_state);
    if (state->charge_done) return "Full";
    if (state->charging) return "Charging";
    if (state->charger_ready || state->gauge_ready) return state->vbus_connected ? "Standby" : "Discharge";
    return "Unavailable";
}

static void add_row(const char *title, const char *value, uint8_t flags) {
    if (row_count >= MAX_ROWS) return;
    copy_text(values[row_count], sizeof(values[row_count]), value);
    rows[row_count].title = title;
    rows[row_count].subtitle = NULL;
    rows[row_count].value = values[row_count];
    rows[row_count].flags = flags;
    ++row_count;
}

static void add_u16(const char *title, uint16_t value, const char *unit, uint8_t flags) {
    char text[VALUE_SIZE];
    snprintf(text, sizeof(text), "%u %s", (unsigned)value, unit ? unit : "");
    add_row(title, text, flags);
}

static void add_i16(const char *title, int16_t value, const char *unit, uint8_t flags) {
    char text[VALUE_SIZE];
    snprintf(text, sizeof(text), "%d %s", (int)value, unit ? unit : "");
    add_row(title, text, flags);
}

static void add_temperature(uint16_t deci_kelvin) {
    char text[VALUE_SIZE];
    if (deci_kelvin == 0) {
        add_row("Temperature", "--", 0);
        return;
    }
    const int deci_c = (int)deci_kelvin - 2731;
    const int fraction = deci_c < 0 ? -(deci_c % 10) : deci_c % 10;
    snprintf(text, sizeof(text), "%d.%d C", deci_c / 10, fraction);
    add_row("Temperature", text, 0);
}

static void build_rows(const t5_battery_state_t *state) {
    row_count = 0;
    memset(rows, 0, sizeof(rows));
    memset(values, 0, sizeof(values));

    add_row("Mode", mode_name(state), T5_UI_LIST_HIGHLIGHT_VALUE);
    add_u16("Charge", state->soc_percent, "%", T5_UI_LIST_HIGHLIGHT_VALUE);
    add_u16("Voltage", state->gauge_read_ok ? state->gauge_voltage_mv : state->battery_voltage_mv,
            "mV", T5_UI_LIST_HIGHLIGHT_VALUE);

    if (!state->detailed_telemetry) {
        add_row("Board", state->board_name, 0);
        add_row("State", gauge_state_name(state->gauge_state), 0);
        return;
    }

    add_i16("Average current", state->average_current_ma, "mA", 0);
    add_i16("Current", state->current_ma, "mA", 0);
    add_u16("Health", state->soh_percent, "%", 0);
    add_u16("Remaining", state->remaining_capacity_mah, "mAh", 0);
    add_u16("Full capacity", state->full_capacity_mah, "mAh", 0);
    add_u16("Battery model", state->profile_capacity_mah, "mAh", 0);
    add_temperature(state->temperature_dk);
    add_row("VBUS", state->vbus_connected ? "IN" : "OUT", 0);

    add_row("BQ27220", online_text(state->gauge_ready, state->gauge_read_ok), T5_UI_LIST_HIGHLIGHT_VALUE);
    add_row("Gauge state", state->gauge_read_ok ? gauge_state_name(state->gauge_state) : "--", 0);
    add_u16("Gauge charge V", state->gauge_charge_voltage_mv, "mV", 0);
    add_u16("Gauge taper I", state->gauge_taper_current_ma, "mA", 0);
    add_row("Battery full flag", yes_no(state->gauge_battery_full_flag), 0);
    add_row("Gauging full flag", yes_no(state->gauge_gauging_full_flag), 0);
    add_row("Taper flag", yes_no(state->gauge_taper_flag), 0);
    add_row("Charge inhibit", yes_no(state->gauge_charge_inhibit), 0);

    add_row("BQ25896", online_text(state->charger_ready, state->charger_read_ok), T5_UI_LIST_HIGHLIGHT_VALUE);
    add_u16("VBUS voltage", state->vbus_voltage_mv, "mV", 0);
    add_u16("System voltage", state->system_voltage_mv, "mV", 0);
    add_u16("Battery voltage", state->battery_voltage_mv, "mV", 0);
    add_u16("Regulation V", state->charge_voltage_mv, "mV", 0);
    add_u16("Charge current", state->charge_current_ma, "mA", 0);
    if (row_count < MAX_ROWS) {
        char text[VALUE_SIZE];
        snprintf(text, sizeof(text), "%u/%u mA", (unsigned)state->precharge_current_ma,
                 (unsigned)state->termination_current_ma);
        add_row("Pre/termination", text, 0);
    }
    if (row_count < MAX_ROWS) {
        char text[VALUE_SIZE];
        snprintf(text, sizeof(text), "%s / %s", charge_status_name(state->charger_status),
                 state->charge_enabled ? "Enabled" : "Disabled");
        add_row("Charge status", text, 0);
    }
}

static void render_state(const t5_battery_state_t *state) {
    build_rows(state);
    if (row_count == 0) selected_index = 0;
    else if (selected_index < 0 || (uint32_t)selected_index >= row_count) selected_index = 0;

    if (!state->available) {
        copy_text(footer, sizeof(footer), "Battery management unavailable");
    } else if (!state->detailed_telemetry) {
        snprintf(footer, sizeof(footer), "%s | %u%% | basic ADC telemetry", state->board_name,
                 (unsigned)state->soc_percent);
    } else {
        snprintf(footer, sizeof(footer), "%s | %u%% | %s | %d mA avg", state->board_name,
                 (unsigned)state->soc_percent, state->vbus_connected ? "USB IN" : "USB OUT",
                 (int)state->average_current_ma);
    }

    const t5_ui_chrome_t chrome = {
        .title = "Battery Status",
        .subtitle = state->detailed_telemetry ? "Gauge and charger telemetry" : "Battery telemetry",
        .status = footer,
        .back_label = "Back",
        .confirm_label = "Update",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, row_count, selected_index);
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    battery = t5_battery_get_api(T5_BATTERY_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !battery || !ui || !app->poll || !battery->read || !ui->render_list || !ui->hit_test ||
        !ui->next_index || !ui->previous_index) return;

    t5_battery_state_t state;
    memset(&state, 0, sizeof(state));
    if (!battery->read(&state)) return;
    selected_index = 0;
    render_state(&state);

    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;

        if (input.buttons & T5_APP_BUTTON_CONFIRM) {
            if (battery->read(&state)) render_state(&state);
            continue;
        }
        if ((input.buttons & T5_APP_BUTTON_UP) || (input.buttons & T5_APP_BUTTON_LEFT)) {
            selected_index = ui->previous_index(selected_index, row_count);
            render_state(&state);
            continue;
        }
        if ((input.buttons & T5_APP_BUTTON_DOWN) || (input.buttons & T5_APP_BUTTON_RIGHT)) {
            selected_index = ui->next_index(selected_index, row_count);
            render_state(&state);
            continue;
        }
        if (input.tapped) {
            const int32_t hit = ui->hit_test(input.touch_x, input.touch_y);
            if (hit >= 0 && (uint32_t)hit < row_count) {
                selected_index = hit;
                render_state(&state);
            }
        }
    }
}
