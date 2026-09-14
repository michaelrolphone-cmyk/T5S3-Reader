#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5GpsApi.h"
#include "T5UiApi.h"

void app_main(void);

static uint32_t now_ms;
static int polls;
static int reads;
static int renders;
static bool started;
static bool stopped;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    now_ms += 1000;
    ++polls;
    if (polls >= 4) input->buttons = T5_APP_BUTTON_BACK;
    return true;
}
static uint32_t app_millis(void) { return now_ms; }
static const t5_app_api_v1 app_api = {
    .abi_version = T5_APP_ABI_VERSION,
    .struct_size = sizeof(t5_app_api_v1),
    .poll = app_poll,
    .millis = app_millis,
};
const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    return version == T5_APP_ABI_VERSION ? &app_api : NULL;
}

static bool gps_supported(void) { return true; }
static bool gps_start(void) { started = true; return true; }
static void gps_stop(void) { stopped = true; }
static bool gps_read(t5_gps_state_t *state) {
    assert(state);
    memset(state, 0, sizeof(*state));
    ++reads;
    state->receiver_detected = 1;
    state->baud = 38400;
    state->satellites = 8;
    state->hdop = 0.9f;
    if (reads >= 2) {
        state->status = T5_GPS_STATUS_FIX;
        state->fix_valid = 1;
        state->latitude = 43.6150187;
        state->longitude = -116.2023137;
        state->altitude_m = 824.5f;
        state->speed_kph = 2.5f;
        state->age_ms = 250;
    } else {
        state->status = T5_GPS_STATUS_SEARCHING;
    }
    return true;
}
static const t5_gps_api_v1 gps_api = {
    .api_version = T5_GPS_API_VERSION,
    .struct_size = sizeof(t5_gps_api_v1),
    .supported = gps_supported,
    .start = gps_start,
    .stop = gps_stop,
    .read = gps_read,
};
const t5_gps_api_v1 *t5_gps_get_api(uint32_t version) {
    return version == T5_GPS_API_VERSION ? &gps_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 7 && selected_index == 0);
    assert(strcmp(chrome->title, "GPS") == 0);
    assert(strcmp(rows[1].title, "Latitude") == 0);
    assert(strcmp(rows[2].title, "Longitude") == 0);
    if (renders == 0) {
        assert(strcmp(rows[0].value, "Searching") == 0);
        assert(strcmp(rows[1].value, "--") == 0);
    } else {
        assert(strcmp(rows[0].value, "Fix") == 0);
        assert(strcmp(rows[1].value, "43.6150187") == 0);
        assert(strcmp(rows[2].value, "-116.2023137") == 0);
    }
    ++renders;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    app_main();
    assert(started);
    assert(stopped);
    assert(polls == 4);
    assert(reads >= 4);
    assert(renders == 2);
    return 0;
}
