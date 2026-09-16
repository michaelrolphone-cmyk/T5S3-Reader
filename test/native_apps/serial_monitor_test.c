#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5SerialPortApi.h"
#include "T5StreamApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);

static int renders;
static int list_renders;
static int polls;
static int writes;
static int acquisitions;
static int releases;
static bool read_once;
static bool read_replacement;
static bool revoked;
static t5_serial_config_t active_config;
static t5_serial_port_lease_t current_lease;

static t5_serial_result_t serial_acquire(const t5_serial_port_request_t *request,
                                         t5_serial_port_lease_t *lease,
                                         t5_stream_t *rx,
                                         t5_stream_t *tx) {
    assert(request && lease && rx && tx && current_lease == 0);
    assert(request->device == 0u);
    assert(request->config.baud_rate == 115200u && request->config.data_bits == 8u &&
           request->config.parity == T5_SERIAL_PARITY_NONE && request->config.stop_bits == 1u &&
           request->config.flow_control == T5_SERIAL_FLOW_NONE);
    assert(acquisitions < 2);
    ++acquisitions;
    active_config = request->config;
    current_lease = acquisitions == 1 ? 7u : 11u;
    *lease = current_lease;
    *rx = acquisitions == 1 ? 1u : 3u;
    *tx = acquisitions == 1 ? 2u : 4u;
    return T5_SERIAL_OK;
}
static t5_serial_result_t serial_configure(t5_serial_port_lease_t lease, const t5_serial_config_t *config) {
    assert(lease == current_lease && config);
    active_config = *config;
    return T5_SERIAL_OK;
}
static t5_serial_result_t serial_status(t5_serial_port_lease_t lease, t5_serial_port_state_t *state) {
    assert(lease == current_lease && state);
    memset(state, 0, sizeof(*state));
    state->config = active_config;
    if (lease == 7u && polls >= 1) {
        revoked = true;
        state->status = T5_SERIAL_STATUS_WAITING;
        state->last_error = T5_SERIAL_DISCONNECTED;
        return T5_SERIAL_OK;
    }
    state->status = T5_SERIAL_STATUS_READY;
    state->connected = 1;
    state->dtr = 1;
    state->rts = 1;
    state->device = lease == 7u ? 9u : 13u;
    strcpy(state->device_label, "Test Serial Provider");
    return T5_SERIAL_OK;
}
static t5_serial_result_t serial_controls(t5_serial_port_lease_t lease, bool dtr, bool rts) {
    assert(lease == current_lease);
    (void)dtr; (void)rts;
    return T5_SERIAL_OK;
}
static t5_serial_result_t serial_release(t5_serial_port_lease_t lease) {
    assert(lease == current_lease && releases < acquisitions);
    if (lease == 7u) assert(revoked && acquisitions == 1);
    if (lease == 11u) assert(acquisitions == 2);
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

static t5_stream_result_t stream_read(t5_stream_t stream, void *data, uint32_t capacity, uint32_t *count) {
    static const uint8_t first[] = {'h','e','l','l','o','\n'};
    static const uint8_t next[] = {'n','e','w','\n'};
    assert(data && count);
    if (stream == 1u) {
        assert(current_lease == 7u && !revoked);
        if (read_once || capacity < sizeof(first)) { *count = 0; return T5_STREAM_AGAIN; }
        memcpy(data, first, sizeof(first));
        *count = sizeof(first);
        read_once = true;
        return T5_STREAM_OK;
    }
    assert(stream == 3u && current_lease == 11u && revoked);
    if (read_replacement || capacity < sizeof(next)) { *count = 0; return T5_STREAM_AGAIN; }
    memcpy(data, next, sizeof(next));
    *count = sizeof(next);
    read_replacement = true;
    return T5_STREAM_OK;
}
static t5_stream_result_t stream_write(t5_stream_t stream, const void *data, uint32_t size, uint32_t *count) {
    assert(stream == 2u && current_lease == 7u && !revoked && data && count);
    assert(size == 6u && memcmp(data, "ping\r\n", 6u) == 0);
    ++writes;
    *count = size;
    return T5_STREAM_OK;
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

static void render_text(const t5_ui_chrome_t *chrome, const char *text, int32_t scroll, t5_ui_text_view_result_t *result) {
    assert(chrome && text && result && scroll == 0);
    assert(strcmp(chrome->title, "Serial Monitor") == 0);
    if (renders > 0) assert(strstr(text, "hello") != NULL || strstr(text, "> ping") != NULL);
    result->max_scroll_lines = 0;
    result->total_lines = 1;
    result->visible_lines = 1;
    ++renders;
}
static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count > 0u);
    assert(selected_index >= 0 && (uint32_t)selected_index < row_count);
    ++list_renders;
}
static int32_t hit_test(int16_t x, int16_t y) {
    (void)x; (void)y;
    return T5_UI_HIT_NONE;
}
static int32_t next_index(int32_t current, uint32_t count) {
    if (!count) return 0;
    return (current + 1) % (int32_t)count;
}
static int32_t previous_index(int32_t current, uint32_t count) {
    if (!count) return 0;
    return current <= 0 ? (int32_t)count - 1 : current - 1;
}
static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    assert(event && wait_ms == 75);
    memset(event, 0, sizeof(*event));
    ++polls;
    if (polls >= 3) event->type = T5_UI_EVENT_BACK;
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
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) { return version == T5_UI_API_VERSION ? &ui_api : NULL; }

static bool keyboard_request(const char *title, const char *initial, size_t max, uint8_t type, uint64_t cookie) {
    (void)title; (void)initial; (void)max; (void)type; (void)cookie; return false;
}
static bool keyboard_take(char *text, size_t capacity, bool *cancelled, uint64_t *cookie) {
    assert(text && capacity > 5 && cancelled && cookie);
    strcpy(text, "ping");
    *cancelled = false;
    *cookie = 0x55534201u;
    return true;
}
static const t5_system_ui_api_v1 sys_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = keyboard_request,
    .keyboard_take_result = keyboard_take,
};
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version) { return version == T5_SYSTEM_UI_API_VERSION ? &sys_api : NULL; }

int main(void) {
    app_main();
    assert(acquisitions == 2 && releases == 2 && current_lease == 0);
    assert(revoked && read_once && read_replacement);
    assert(writes == 1); /* Only the original device received the pending text. */
    assert(renders >= 3);
    assert(list_renders == 0);
    return 0;
}
