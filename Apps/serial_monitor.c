#include "T5AppApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TERMINAL_CAP 12288u
#define SEND_CAP 257u
#define COOKIE_SEND 0x55534201u

static const t5_usb_api_v1 *usb;
static const t5_ui_api_v1 *ui;
static const t5_system_ui_api_v1 *system_ui;
static char terminal[TERMINAL_CAP];
static size_t terminal_len;
static int32_t scroll_from_bottom;
static char status_text[128];

static void append_char(char c) {
    if (c == '\r') return;
    if (terminal_len + 1u >= sizeof(terminal)) {
        size_t keep = sizeof(terminal) / 2u;
        size_t start = terminal_len > keep ? terminal_len - keep : 0u;
        size_t i;
        for (i = 0; i < terminal_len - start; ++i) terminal[i] = terminal[start + i];
        terminal_len -= start;
    }
    terminal[terminal_len++] = c;
    terminal[terminal_len] = 0;
}

static void append_bytes(const uint8_t *data, size_t length) {
    size_t i;
    for (i = 0; i < length; ++i) {
        uint8_t c = data[i];
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 32u && c <= 126u)) append_char((char)c);
        else append_char('.');
    }
}

static const char *status_name(uint8_t status) {
    switch (status) {
        case T5_USB_STATUS_OFF: return "Off";
        case T5_USB_STATUS_WAITING: return "Waiting for USB CDC device";
        case T5_USB_STATUS_CONFIGURING: return "Configuring USB CDC";
        case T5_USB_STATUS_READY: return "Connected";
        case T5_USB_STATUS_ERROR: return "USB host error";
        default: return "USB OTG unsupported";
    }
}

static void render(const t5_usb_serial_state_t *state) {
    t5_ui_text_view_result_t result = {0};
    if (state->status == T5_USB_STATUS_READY) {
        snprintf(status_text, sizeof(status_text), "%s  %04X:%04X  %lu 8N1",
                 state->product[0] ? state->product : "USB CDC",
                 (unsigned)state->vid, (unsigned)state->pid,
                 (unsigned long)state->line_coding.baud_rate);
    } else if (state->status == T5_USB_STATUS_ERROR) {
        snprintf(status_text, sizeof(status_text), "%s (%ld)", status_name(state->status), (long)state->last_error);
    } else {
        snprintf(status_text, sizeof(status_text), "%s", status_name(state->status));
    }

    const t5_ui_chrome_t chrome = {
        .title = "USB Serial",
        .subtitle = "USB OTG / CDC-ACM",
        .status = status_text,
        .back_label = "Stop",
        .confirm_label = state->status == T5_USB_STATUS_READY ? "Send" : "",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_text_view(&chrome, terminal_len ? terminal : "Connect a USB CDC serial device to the USB-C OTG port.",
                         scroll_from_bottom, &result);
    if (scroll_from_bottom > result.max_scroll_lines) scroll_from_bottom = result.max_scroll_lines;
}

void app_main(void) {
    usb = t5_usb_get_api(T5_USB_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    if (!usb || !ui || !system_ui || !usb->supported || !usb->serial_start || !usb->serial_stop ||
        !usb->serial_read_state || !usb->serial_read || !usb->serial_write || !ui->render_text_view ||
        !ui->poll_event || !system_ui->keyboard_request || !system_ui->keyboard_take_result) return;

    t5_usb_line_coding_t coding = {115200u, 8u, T5_USB_PARITY_NONE, 1u, 0u};
    t5_usb_serial_state_t state = {0};
    char send_text[SEND_CAP];
    bool cancelled = false;
    uint64_t cookie = 0;

    if (!usb->supported()) {
        state.status = T5_USB_STATUS_UNSUPPORTED;
    } else {
        (void)usb->serial_start(&coding); /* Idempotent so keyboard relaunch keeps the host session. */
        (void)usb->serial_read_state(&state);
    }

    if (system_ui->keyboard_take_result(send_text, sizeof(send_text), &cancelled, &cookie) &&
        cookie == COOKIE_SEND && !cancelled && send_text[0] && state.status == T5_USB_STATUS_READY) {
        size_t length = 0;
        while (send_text[length]) ++length;
        (void)usb->serial_write((const uint8_t *)send_text, length);
        (void)usb->serial_write((const uint8_t *)"\r\n", 2u);
        append_bytes((const uint8_t *)"> ", 2u);
        append_bytes((const uint8_t *)send_text, length);
        append_char('\n');
    }

    render(&state);
    for (;;) {
        uint8_t incoming[256];
        size_t received = usb->serial_read(incoming, sizeof(incoming));
        bool repaint = false;
        if (received) {
            append_bytes(incoming, received);
            scroll_from_bottom = 0;
            repaint = true;
        }
        t5_usb_serial_state_t next = {0};
        if (usb->serial_read_state(&next)) {
            if (next.status != state.status || next.vid != state.vid || next.pid != state.pid ||
                next.rx_bytes != state.rx_bytes || next.tx_bytes != state.tx_bytes) repaint = true;
            state = next;
        }
        if (repaint) render(&state);

        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 75)) break;
        if (event.type == T5_UI_EVENT_EXIT || event.type == T5_UI_EVENT_BACK) {
            usb->serial_stop();
            return;
        }
        if (event.type == T5_UI_EVENT_PREVIOUS) {
            ++scroll_from_bottom;
            render(&state);
        } else if (event.type == T5_UI_EVENT_NEXT) {
            if (scroll_from_bottom > 0) --scroll_from_bottom;
            render(&state);
        } else if (event.type == T5_UI_EVENT_CONFIRM && state.status == T5_USB_STATUS_READY) {
            if (system_ui->keyboard_request("Send over USB serial", "", SEND_CAP - 1u,
                                            T5_SYSTEM_KEYBOARD_TEXT, COOKIE_SEND)) {
                return; /* Firmware owns keyboard handoff; USB service remains active. */
            }
        }
    }
    usb->serial_stop();
}
