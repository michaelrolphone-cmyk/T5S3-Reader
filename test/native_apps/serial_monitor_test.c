#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5SystemUiApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"

void app_main(void);

static int renders;
static int polls;
static int writes;
static bool started;
static bool stopped;
static bool read_once;

static bool usb_supported(void) { return true; }
static bool usb_start(const t5_usb_line_coding_t *coding) {
    assert(coding && coding->baud_rate == 115200u && coding->data_bits == 8u &&
           coding->parity == T5_USB_PARITY_NONE && coding->stop_bits == 1u);
    started = true;
    return true;
}
static void usb_stop(void) { stopped = true; }
static bool usb_state(t5_usb_serial_state_t *state) {
    assert(state);
    memset(state, 0, sizeof(*state));
    state->status = T5_USB_STATUS_READY;
    state->connected = 1;
    state->vid = 0x303a;
    state->pid = 0x1001;
    state->line_coding.baud_rate = 115200u;
    state->line_coding.data_bits = 8;
    state->line_coding.stop_bits = 1;
    strcpy(state->product, "Test CDC");
    return true;
}
static bool usb_line(const t5_usb_line_coding_t *coding) { return coding != NULL; }
static bool usb_controls(bool dtr, bool rts) { return dtr || rts; }
static size_t usb_read(uint8_t *data, size_t capacity) {
    static const uint8_t msg[] = {'h','e','l','l','o','\n'};
    if (read_once || capacity < sizeof(msg)) return 0;
    memcpy(data, msg, sizeof(msg));
    read_once = true;
    return sizeof(msg);
}
static size_t usb_write(const uint8_t *data, size_t length) {
    assert(data && length > 0);
    ++writes;
    return length;
}
static const t5_usb_api_v1 usb_api = {
    .api_version = T5_USB_API_VERSION,
    .struct_size = sizeof(t5_usb_api_v1),
    .supported = usb_supported,
    .serial_start = usb_start,
    .serial_stop = usb_stop,
    .serial_read_state = usb_state,
    .serial_set_line_coding = usb_line,
    .serial_set_control_lines = usb_controls,
    .serial_read = usb_read,
    .serial_write = usb_write,
};
const t5_usb_api_v1 *t5_usb_get_api(uint32_t version) { return version == T5_USB_API_VERSION ? &usb_api : NULL; }

static void render_text(const t5_ui_chrome_t *chrome, const char *text, int32_t scroll, t5_ui_text_view_result_t *result) {
    assert(chrome && text && result && scroll == 0);
    assert(strcmp(chrome->title, "USB Serial") == 0);
    if (renders > 0) assert(strstr(text, "hello") != NULL);
    result->max_scroll_lines = 0;
    result->total_lines = 1;
    result->visible_lines = 1;
    ++renders;
}
static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    assert(event && wait_ms == 75);
    memset(event, 0, sizeof(*event));
    ++polls;
    if (polls >= 2) event->type = T5_UI_EVENT_BACK;
    return true;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .poll_event = poll_event,
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
    assert(started);
    assert(stopped);
    assert(read_once);
    assert(writes == 2); /* text + CRLF */
    assert(renders >= 2);
    return 0;
}
