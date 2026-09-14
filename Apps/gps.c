#include "T5AppApi.h"
#include "T5GpsApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const t5_app_api_v1 *app;
static const t5_gps_api_v1 *gps;
static const t5_ui_api_v1 *ui;

static char status_value[32];
static char latitude_value[32];
static char longitude_value[32];
static char satellites_value[16];
static char footer[96];

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

static const char *status_name(uint8_t status) {
    switch (status) {
        case T5_GPS_STATUS_FIX: return "Fix";
        case T5_GPS_STATUS_SEARCHING: return "Searching";
        case T5_GPS_STATUS_OFF: return "Off";
        default: return "Unsupported";
    }
}

static void render(const t5_gps_state_t *state) {
    copy_text(status_value, sizeof(status_value), status_name(state->status));
    if (state->fix_valid) {
        snprintf(latitude_value, sizeof(latitude_value), "%.7f", state->latitude);
        snprintf(longitude_value, sizeof(longitude_value), "%.7f", state->longitude);
    } else {
        copy_text(latitude_value, sizeof(latitude_value), "--");
        copy_text(longitude_value, sizeof(longitude_value), "--");
    }
    snprintf(satellites_value, sizeof(satellites_value), "%u", (unsigned)state->satellites);

    if (state->status == T5_GPS_STATUS_UNSUPPORTED) {
        copy_text(footer, sizeof(footer), "GPS hardware is not available on this board");
    } else if (!state->receiver_detected) {
        snprintf(footer, sizeof(footer), "Probing receiver at %lu baud", (unsigned long)state->baud);
    } else if (!state->fix_valid) {
        copy_text(footer, sizeof(footer), "Receiver detected; waiting for satellite fix");
    } else {
        snprintf(footer, sizeof(footer), "Fix age %lu ms | %lu baud", (unsigned long)state->age_ms,
                 (unsigned long)state->baud);
    }

    const t5_ui_chrome_t chrome = {
        .title = "GPS",
        .subtitle = "Current coordinates",
        .status = footer,
        .back_label = "Back",
        .confirm_label = "",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t rows[] = {
        {.title = "Status", .subtitle = NULL, .value = status_value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "Latitude", .subtitle = NULL, .value = latitude_value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "Longitude", .subtitle = NULL, .value = longitude_value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "Satellites", .subtitle = NULL, .value = satellites_value, .flags = 0},
    };
    ui->render_list(&chrome, rows, (uint32_t)(sizeof(rows) / sizeof(rows[0])), 0);
}

void app_main(void) {
    app = t5_app_get_api(T5_APP_ABI_VERSION);
    gps = t5_gps_get_api(T5_GPS_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!app || !gps || !ui || !app->poll || !app->millis || !gps->supported || !gps->start || !gps->stop ||
        !gps->read || !ui->render_list) return;

    t5_gps_state_t state;
    memset(&state, 0, sizeof(state));
    if (!gps->supported()) {
        state.status = T5_GPS_STATUS_UNSUPPORTED;
    } else if (!gps->start()) {
        state.status = T5_GPS_STATUS_OFF;
    } else {
        gps->read(&state);
    }
    render(&state);

    uint32_t last_render = app->millis();
    for (;;) {
        t5_app_input_t input;
        if (!app->poll(&input, 50) || input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK)) break;
        gps->read(&state);
        const uint32_t now = app->millis();
        if (now - last_render >= 3000u) {
            render(&state);
            last_render = now;
        }
    }

    gps->stop();
}
