#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5SerialPortApi.h"
#include "T5StreamApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"

void app_main(void);
static unsigned acquisitions, releases, initial_lists, terminal_renders, polls;

static t5_serial_result_t acquire(const t5_serial_port_request_t* request,
                                  t5_serial_port_lease_t* lease,
                                  t5_stream_t* rx, t5_stream_t* tx) {
    assert(request && request->device == 0 && lease && rx && tx);
    assert(request->config.baud_rate == 115200u);
    assert(*lease == 0 && *rx == 0 && *tx == 0);
    ++acquisitions;
    return T5_SERIAL_IO; /* No device, or host/power provider unavailable. */
}
static t5_serial_result_t configure(t5_serial_port_lease_t l, const t5_serial_config_t* c) {
    (void)l; (void)c; assert(false); return T5_SERIAL_IO;
}
static t5_serial_result_t status(t5_serial_port_lease_t l, t5_serial_port_state_t* s) {
    (void)l; (void)s; assert(false); return T5_SERIAL_IO;
}
static t5_serial_result_t controls(t5_serial_port_lease_t l, bool d, bool r) {
    (void)l; (void)d; (void)r; assert(false); return T5_SERIAL_IO;
}
static t5_serial_result_t release(t5_serial_port_lease_t l) {
    (void)l; ++releases; assert(false); return T5_SERIAL_IO;
}
static const t5_serial_port_api_v1 serial_api = {
    .api_version = T5_SERIAL_PORT_API_VERSION,
    .struct_size = sizeof(t5_serial_port_api_v1),
    .capability_id = T5_SERIAL_PORT_CAPABILITY,
    .acquire = acquire, .configure = configure, .read_status = status,
    .set_control_lines = controls, .release = release,
};
const t5_serial_port_api_v1* t5_serial_port_get_api(uint32_t version) {
    return version == T5_SERIAL_PORT_API_VERSION ? &serial_api : NULL;
}
static t5_stream_result_t read_bytes(t5_stream_t s, void* data, uint32_t n, uint32_t* done) {
    (void)s; (void)data; (void)n; (void)done; assert(false); return T5_STREAM_IO;
}
static t5_stream_result_t write_bytes(t5_stream_t s, const void* data, uint32_t n, uint32_t* done) {
    (void)s; (void)data; (void)n; (void)done; assert(false); return T5_STREAM_IO;
}
static const t5_stream_api_v1 stream_api = {
    .api_version = T5_STREAM_API_VERSION, .struct_size = sizeof(t5_stream_api_v1),
    .read = read_bytes, .write = write_bytes,
};
const t5_stream_api_v1* t5_stream_get_api(uint32_t version) {
    return version == T5_STREAM_API_VERSION ? &stream_api : NULL;
}
static void draw_list(const t5_ui_chrome_t* chrome, const t5_ui_list_row_t* rows,
                      uint32_t count, int32_t selected) {
    assert(chrome && rows && count == 1u && selected == 0);
    assert(strcmp(chrome->title, "Serial Monitor") == 0);
    assert(rows[0].title && strstr(rows[0].title, "Initializing serial session"));
    ++initial_lists; /* Failed reconnects must not replace the terminal. */
}
static void draw_terminal(const t5_ui_chrome_t* chrome, const char* text,
                          int32_t scroll, t5_ui_text_view_result_t* result) {
    assert(chrome && text && result && scroll == 0);
    assert(strcmp(chrome->title, "Serial Monitor") == 0);
    assert(strcmp(chrome->back_label, "Stop") == 0);
    assert(strstr(chrome->status, "Serial unavailable"));
    assert(strstr(text, "serial.port acquire") || strstr(text, "Unable to acquire"));
    result->max_scroll_lines = 0;
    ++terminal_renders;
}
static int32_t hit(int16_t x, int16_t y) { (void)x; (void)y; return T5_UI_HIT_NONE; }
static int32_t next(int32_t i, uint32_t n) { return n ? (i + 1) % (int32_t)n : 0; }
static int32_t previous(int32_t i, uint32_t n) { return n ? (i ? i - 1 : (int32_t)n - 1) : 0; }
static bool poll(t5_ui_event_t* event, uint32_t wait) {
    assert(event && wait == 75u);
    memset(event, 0, sizeof(*event));
    if (++polls == 40u) event->type = T5_UI_EVENT_BACK;
    return true;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION, .struct_size = sizeof(t5_ui_api_v1),
    .render_list = draw_list, .hit_test = hit, .poll_event = poll,
    .next_index = next, .previous_index = previous,
    .render_text_view = draw_terminal,
};
const t5_ui_api_v1* t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}
static bool keyboard_request(const char* title, const char* initial, size_t max,
                             uint8_t type, uint64_t cookie) {
    (void)title; (void)initial; (void)max; (void)type; (void)cookie;
    return false;
}
static bool keyboard_take(char* text, size_t n, bool* cancelled, uint64_t* cookie) {
    (void)text; (void)n; (void)cancelled; (void)cookie;
    return false;
}
static const t5_system_ui_api_v1 sys_api = {
    .api_version = T5_SYSTEM_UI_API_VERSION,
    .struct_size = sizeof(t5_system_ui_api_v1),
    .keyboard_request = keyboard_request, .keyboard_take_result = keyboard_take,
};
const t5_system_ui_api_v1* t5_system_ui_get_api(uint32_t version) {
    return version == T5_SYSTEM_UI_API_VERSION ? &sys_api : NULL;
}
int main(void) {
    app_main();
    assert(initial_lists == 1u && terminal_renders >= 1u);
    assert(polls == 40u && acquisitions == 1u && releases == 0u);
    return 0;
}
