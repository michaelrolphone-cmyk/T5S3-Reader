#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5SerialPortApi.h"
#include "T5StreamApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);

static int acquisitions;
static int releases;
static int configure_calls;
static int polls;
static t5_serial_port_lease_t current_lease;
static t5_serial_config_t active_config;
static bool saw_9600_status;

static t5_serial_result_t serial_acquire(const t5_serial_port_request_t *request,
                                         t5_serial_port_lease_t *lease,
                                         t5_stream_t *rx,
                                         t5_stream_t *tx) {
    assert(request && lease && rx && tx && current_lease == 0);
    assert(request->device == 0u);
    ++acquisitions;
    if (acquisitions == 1) assert(request->config.baud_rate == 115200u);
    if (acquisitions == 2) assert(request->config.baud_rate == 9600u);
    assert(acquisitions <= 2);
    active_config = request->config;
    current_lease = (t5_serial_port_lease_t)(10 + acquisitions);
    *lease = current_lease;
    *rx = (t5_stream_t)(20 + acquisitions);
    *tx = (t5_stream_t)(30 + acquisitions);
    return T5_SERIAL_OK;
}

static t5_serial_result_t serial_configure(t5_serial_port_lease_t lease,
                                           const t5_serial_config_t *config) {
    assert(lease == current_lease && config);
    ++configure_calls;
    return T5_SERIAL_IO; /* Live recoding is intentionally unavailable. */
}

static t5_serial_result_t serial_status(t5_serial_port_lease_t lease,
                                        t5_serial_port_state_t *state) {
    assert(lease == current_lease && state);
    memset(state, 0, sizeof(*state));
    state->status = T5_SERIAL_STATUS_READY;
    state->connected = 1;
    state->device = 77u;
    state->config = active_config;
    strcpy(state->device_label, "Baud Test");
    return T5_SERIAL_OK;
}

static t5_serial_result_t serial_controls(t5_serial_port_lease_t lease, bool dtr, bool rts) {
    assert(lease == current_lease);
    (void)dtr;
    (void)rts;
    return T5_SERIAL_OK;
}

static t5_serial_result_t serial_release(t5_serial_port_lease_t lease) {
    assert(lease == current_lease);
    ++releases;
    current_lease = 0;
    return T5_SERIAL_OK;
}

static const t5_serial_port_api_v1 serial_api = {
    .api_version = T5_SERIAL_PORT_API_VERSION,
    .struct_size = sizeof(t5_serial_port_api_v1),
    .capability_id = T5_SERIAL_PORT_CAPABILITY,
    .acquire = serial_acquire,
    .configure = serial_configure,
    .read_status = serial_status,
    .set_control_lines = serial_controls,
    .release = serial_release,
};

const t5_serial_port_api_v1 *t5_serial_port_get_api(uint32_t version) {
    return version == T5_SERIAL_PORT_API_VERSION ? &serial_api : NULL;
}

static t5_stream_result_t stream_read(t5_stream_t stream, void *data,
                                      uint32_t capacity, uint32_t *count) {
    assert((stream == 21u || stream == 22u) && data && capacity && count);
    *count = 0;
    return T5_STREAM_AGAIN;
}

static t5_stream_result_t stream_write(t5_stream_t stream, const void *data,
                                       uint32_t size, uint32_t *count) {
    (void)stream;
    (void)data;
    (void)size;
    if (count) *count = 0;
    return T5_STREAM_IO;
}

static const t5_stream_api_v1 stream_api = {
    .api_version = T5_STREAM_API_VERSION,
    .struct_size = sizeof(t5_stream_api_v1),
    .read = stream_read,
    .write = stream_write,
};

const t5_stream_api_v1 *t5_stream_get_api(uint32_t version) {
    return version == T5_STREAM_API_VERSION ? &stream_api : NULL;
}

static void render_text(const t5_ui_chrome_t *chrome, const char *text,
                        int32_t scroll, t5_ui_text_view_result_t *result) {
    assert(chrome && text && result && scroll == 0);
    if (chrome->status && strstr(chrome->status, "9600 8N1")) saw_9600_status = true;
    result->max_scroll_lines = 0;
    result->total_lines = 1;
    result->visible_lines = 1;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count > 0u && selected_index >= 0);
}

static int32_t hit_test(int16_t x, int16_t y) {
    (void)x;
    (void)y;
    return 3; /* 9600 in the baud list. */
}

static int32_t next_index(int32_t current, uint32_t count) {
    return count ? (current + 1) % (int32_t)count : 0;
}

static int32_t previous_index(int32_t current, uint32_t count) {
    return count ? (current <= 0 ? (int32_t)count - 1 : current - 1) : 0;
}

static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    assert(event && wait_ms == 75u);
    memset(event, 0, sizeof(*event));
    ++polls;
    switch (polls) {
        case 1: event->type = T5_UI_EVENT_CONFIRM; break; /* Terminal -> Actions */
        case 2: event->type = T5_UI_EVENT_NEXT; break;
        case 3: event->type = T5_UI_EVENT_NEXT; break;    /* Select Baud */
        case 4: event->type = T5_UI_EVENT_CONFIRM; break; /* Open baud list */
        case 5:
            event->type = T5_UI_EVENT_TAP;                /* Pick 9600 */
            event->touch_x = 10;
            event->touch_y = 10;
            break;
        case 6: event->type = T5_UI_EVENT_BACK; break;    /* Actions -> Terminal */
        default: event->type = T5_UI_EVENT_BACK; break;   /* Exit */
    }
    return true;
}

static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .hit_test = hit_test,
    .poll_event = poll_event,
    .next_index = next_index,
    .previous_index = previous_index,
    .render_text_view = render_text,
};

const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

static bool keyboard_request(const char *title, const char *initial, size_t max,
                             uint8_t type, uint64_t cookie) {
    (void)title;
    (void)initial;
    (void)max;
    (void)type;
    (void)cookie;
    return false;
}

static bool keyboard_take(char *text, size_t capacity, bool *cancelled, uint64_t *cookie) {
    assert(text && capacity && cancelled && cookie);
    return false;
}

static const t5_system_ui_api_v1 sys_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = keyboard_request,
    .keyboard_take_result = keyboard_take,
};

const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &sys_api : NULL;
}

int main(void) {
    app_main();
    assert(acquisitions == 2);
    assert(releases == 2);
    assert(configure_calls == 0);
    assert(current_lease == 0);
    assert(active_config.baud_rate == 9600u);
    assert(saw_9600_status);
    return 0;
}
