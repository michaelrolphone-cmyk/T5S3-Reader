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
#define COOKIE_CUSTOM_BAUD 0x55534202u

static const t5_usb_api_v1 *usb;
static const t5_ui_api_v1 *ui;
static const t5_system_ui_api_v1 *system_ui;
static char terminal[TERMINAL_CAP];
static size_t terminal_len;
static int32_t scroll_from_bottom;
static char status_text[128];

typedef enum {
    VIEW_TERMINAL = 0,
    VIEW_ACTIONS,
    VIEW_BAUD,
    VIEW_DATA_BITS,
    VIEW_PARITY,
    VIEW_STOP_BITS,
} view_mode_t;

static const uint32_t baud_rates[] = {
    1200u, 2400u, 4800u, 9600u, 19200u, 38400u,
    57600u, 115200u, 230400u, 460800u, 921600u,
};

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

static void append_notice(const char *text) {
    append_char('[');
    while (text && *text) append_char(*text++);
    append_char(']');
    append_char('\n');
}

static bool parse_baud(const char *text, uint32_t *baud) {
    uint32_t value = 0;
    if (!text || !text[0] || !baud) return false;
    while (*text) {
        if (*text < '0' || *text > '9') return false;
        uint32_t digit = (uint32_t)(*text - '0');
        if (value > (3000000u - digit) / 10u) return false;
        value = value * 10u + digit;
        ++text;
    }
    if (value < 300u || value > 3000000u) return false;
    *baud = value;
    return true;
}

static const char *status_name(uint8_t status) {
    switch (status) {
        case T5_USB_STATUS_OFF: return "Off";
        case T5_USB_STATUS_WAITING: return "Waiting for USB serial device";
        case T5_USB_STATUS_CONFIGURING: return "Configuring USB serial";
        case T5_USB_STATUS_READY: return "Connected";
        case T5_USB_STATUS_ERROR: return "USB host error";
        default: return "USB OTG unsupported";
    }
}

static const char *parity_name(uint8_t parity) {
    switch (parity) {
        case T5_USB_PARITY_ODD: return "Odd";
        case T5_USB_PARITY_EVEN: return "Even";
        case T5_USB_PARITY_MARK: return "Mark";
        case T5_USB_PARITY_SPACE: return "Space";
        default: return "None";
    }
}

static char parity_letter(uint8_t parity) {
    switch (parity) {
        case T5_USB_PARITY_ODD: return 'O';
        case T5_USB_PARITY_EVEN: return 'E';
        case T5_USB_PARITY_MARK: return 'M';
        case T5_USB_PARITY_SPACE: return 'S';
        default: return 'N';
    }
}

static void render_terminal(const t5_usb_serial_state_t *state) {
    t5_ui_text_view_result_t result = {0};
    if (state->status == T5_USB_STATUS_READY) {
        snprintf(status_text, sizeof(status_text), "%s  %04X:%04X  %lu %u%c%u",
                 state->product[0] ? state->product : "USB serial",
                 (unsigned)state->vid, (unsigned)state->pid,
                 (unsigned long)state->line_coding.baud_rate,
                 (unsigned)state->line_coding.data_bits,
                 parity_letter(state->line_coding.parity),
                 (unsigned)state->line_coding.stop_bits);
    } else if (state->status == T5_USB_STATUS_ERROR) {
        snprintf(status_text, sizeof(status_text), "%s (%ld)", status_name(state->status), (long)state->last_error);
    } else {
        snprintf(status_text, sizeof(status_text), "%s  %lu %u%c%u",
                 status_name(state->status),
                 (unsigned long)state->line_coding.baud_rate,
                 (unsigned)state->line_coding.data_bits,
                 parity_letter(state->line_coding.parity),
                 (unsigned)state->line_coding.stop_bits);
    }

    const t5_ui_chrome_t chrome = {
        .title = "USB Serial",
        .subtitle = "USB OTG / CDC + UART bridges",
        .status = status_text,
        .back_label = "Stop",
        .confirm_label = "Actions",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_text_view(&chrome, terminal_len ? terminal : "Connect a USB serial device to the USB-C OTG port.\n\nUse Actions to choose baud rate and line settings.",
                         scroll_from_bottom, &result);
    if (scroll_from_bottom > result.max_scroll_lines) scroll_from_bottom = result.max_scroll_lines;
}

static void render_actions(const t5_usb_serial_state_t *state, int32_t selected) {
    char baud[16];
    char data_bits[8];
    char stop_bits[8];
    snprintf(baud, sizeof(baud), "%lu", (unsigned long)state->line_coding.baud_rate);
    snprintf(data_bits, sizeof(data_bits), "%u", (unsigned)state->line_coding.data_bits);
    snprintf(stop_bits, sizeof(stop_bits), "%u", (unsigned)state->line_coding.stop_bits);
    const t5_ui_list_row_t rows[] = {
        {"Send text", NULL, state->status == T5_USB_STATUS_READY ? "Ready" : "Connect first", T5_UI_LIST_HIGHLIGHT_VALUE},
        {"Baud rate", NULL, baud, T5_UI_LIST_HIGHLIGHT_VALUE},
        {"Data bits", NULL, data_bits, T5_UI_LIST_HIGHLIGHT_VALUE},
        {"Parity", NULL, parity_name(state->line_coding.parity), T5_UI_LIST_HIGHLIGHT_VALUE},
        {"Stop bits", NULL, stop_bits, T5_UI_LIST_HIGHLIGHT_VALUE},
        {"DTR", NULL, state->dtr ? "On" : "Off", T5_UI_LIST_HIGHLIGHT_VALUE},
        {"RTS", NULL, state->rts ? "On" : "Off", T5_UI_LIST_HIGHLIGHT_VALUE},
    };
    const t5_ui_chrome_t chrome = {
        .title = "USB Serial",
        .subtitle = "Actions / line settings",
        .status = status_name(state->status),
        .back_label = "Terminal",
        .confirm_label = "Select",
        .previous_label = "Up",
        .next_label = "Down",
    };
    ui->render_list(&chrome, rows, sizeof(rows) / sizeof(rows[0]), selected);
}

static void render_baud(int32_t selected) {
    const t5_ui_list_row_t rows[] = {
        {"1200", NULL, NULL, 0}, {"2400", NULL, NULL, 0}, {"4800", NULL, NULL, 0},
        {"9600", NULL, NULL, 0}, {"19200", NULL, NULL, 0}, {"38400", NULL, NULL, 0},
        {"57600", NULL, NULL, 0}, {"115200", NULL, NULL, 0}, {"230400", NULL, NULL, 0},
        {"460800", NULL, NULL, 0}, {"921600", NULL, NULL, 0}, {"Custom...", NULL, NULL, 0},
    };
    const t5_ui_chrome_t chrome = {
        .title = "Baud rate", .subtitle = "USB Serial", .status = "300 - 3000000 baud",
        .back_label = "Back", .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    ui->render_list(&chrome, rows, sizeof(rows) / sizeof(rows[0]), selected);
}

static void render_data_bits(int32_t selected) {
    const t5_ui_list_row_t rows[] = {
        {"5 bits", NULL, NULL, 0}, {"6 bits", NULL, NULL, 0},
        {"7 bits", NULL, NULL, 0}, {"8 bits", NULL, NULL, 0},
    };
    const t5_ui_chrome_t chrome = {
        .title = "Data bits", .subtitle = "USB Serial", .status = NULL,
        .back_label = "Back", .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    ui->render_list(&chrome, rows, sizeof(rows) / sizeof(rows[0]), selected);
}

static void render_parity(int32_t selected) {
    const t5_ui_list_row_t rows[] = {
        {"None", NULL, NULL, 0}, {"Odd", NULL, NULL, 0}, {"Even", NULL, NULL, 0},
        {"Mark", NULL, NULL, 0}, {"Space", NULL, NULL, 0},
    };
    const t5_ui_chrome_t chrome = {
        .title = "Parity", .subtitle = "USB Serial", .status = NULL,
        .back_label = "Back", .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    ui->render_list(&chrome, rows, sizeof(rows) / sizeof(rows[0]), selected);
}

static void render_stop_bits(int32_t selected) {
    const t5_ui_list_row_t rows[] = {
        {"1 stop bit", NULL, NULL, 0}, {"2 stop bits", NULL, NULL, 0},
    };
    const t5_ui_chrome_t chrome = {
        .title = "Stop bits", .subtitle = "USB Serial", .status = NULL,
        .back_label = "Back", .confirm_label = "Select", .previous_label = "Up", .next_label = "Down",
    };
    ui->render_list(&chrome, rows, sizeof(rows) / sizeof(rows[0]), selected);
}

static void render_view(view_mode_t mode, const t5_usb_serial_state_t *state, int32_t selected) {
    switch (mode) {
        case VIEW_ACTIONS: render_actions(state, selected); break;
        case VIEW_BAUD: render_baud(selected); break;
        case VIEW_DATA_BITS: render_data_bits(selected); break;
        case VIEW_PARITY: render_parity(selected); break;
        case VIEW_STOP_BITS: render_stop_bits(selected); break;
        default: render_terminal(state); break;
    }
}

static int32_t baud_index(uint32_t baud) {
    size_t i;
    for (i = 0; i < sizeof(baud_rates) / sizeof(baud_rates[0]); ++i) {
        if (baud_rates[i] == baud) return (int32_t)i;
    }
    return (int32_t)(sizeof(baud_rates) / sizeof(baud_rates[0]));
}

static bool apply_coding(t5_usb_serial_state_t *state, t5_usb_line_coding_t coding) {
    if (!usb->serial_set_line_coding(&coding)) {
        append_notice("Unable to apply line settings");
        return false;
    }
    state->line_coding = coding;
    return true;
}

static bool state_changed(const t5_usb_serial_state_t *a, const t5_usb_serial_state_t *b) {
    return a->status != b->status || a->connected != b->connected || a->vid != b->vid || a->pid != b->pid ||
           a->last_error != b->last_error || a->rx_bytes != b->rx_bytes || a->tx_bytes != b->tx_bytes ||
           a->dtr != b->dtr || a->rts != b->rts ||
           a->line_coding.baud_rate != b->line_coding.baud_rate ||
           a->line_coding.data_bits != b->line_coding.data_bits ||
           a->line_coding.parity != b->line_coding.parity ||
           a->line_coding.stop_bits != b->line_coding.stop_bits;
}

void app_main(void) {
    usb = t5_usb_get_api(T5_USB_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    if (!usb || !ui || !system_ui || !usb->supported || !usb->serial_start || !usb->serial_stop ||
        !usb->serial_read_state || !usb->serial_set_line_coding || !usb->serial_set_control_lines ||
        !usb->serial_read || !usb->serial_write || !ui->render_text_view || !ui->render_list ||
        !ui->hit_test || !ui->poll_event || !ui->next_index || !ui->previous_index ||
        !system_ui->keyboard_request || !system_ui->keyboard_take_result) return;

    t5_usb_serial_state_t state = {0};
    t5_usb_line_coding_t coding = {115200u, 8u, T5_USB_PARITY_NONE, 1u, 0u};
    char keyboard_text[SEND_CAP];
    bool cancelled = false;
    uint64_t cookie = 0;

    if (!usb->supported()) {
        state.status = T5_USB_STATUS_UNSUPPORTED;
        state.line_coding = coding;
    } else {
        t5_usb_serial_state_t existing = {0};
        if (usb->serial_read_state(&existing) && existing.line_coding.baud_rate >= 300u)
            coding = existing.line_coding;
        (void)usb->serial_start(&coding); /* Idempotent so keyboard relaunch keeps the host session. */
        (void)usb->serial_read_state(&state);
    }

    if (system_ui->keyboard_take_result(keyboard_text, sizeof(keyboard_text), &cancelled, &cookie) && !cancelled) {
        if (cookie == COOKIE_SEND && keyboard_text[0] && state.status == T5_USB_STATUS_READY) {
            size_t length = 0;
            while (keyboard_text[length]) ++length;
            (void)usb->serial_write((const uint8_t *)keyboard_text, length);
            (void)usb->serial_write((const uint8_t *)"\r\n", 2u);
            append_bytes((const uint8_t *)"> ", 2u);
            append_bytes((const uint8_t *)keyboard_text, length);
            append_char('\n');
        } else if (cookie == COOKIE_CUSTOM_BAUD && keyboard_text[0]) {
            uint32_t value = 0;
            if (parse_baud(keyboard_text, &value)) {
                coding = state.line_coding;
                coding.baud_rate = value;
                (void)apply_coding(&state, coding);
            } else {
                append_notice("Invalid baud; enter 300-3000000");
            }
        }
    }

    view_mode_t mode = VIEW_TERMINAL;
    int32_t selected = 0;
    render_view(mode, &state, selected);

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
            if (state_changed(&state, &next)) repaint = true;
            state = next;
        }
        if (repaint && (mode == VIEW_TERMINAL || mode == VIEW_ACTIONS)) render_view(mode, &state, selected);

        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 75)) break;
        if (event.type == T5_UI_EVENT_EXIT) {
            usb->serial_stop();
            return;
        }
        if (event.type == T5_UI_EVENT_BACK) {
            if (mode == VIEW_TERMINAL) {
                usb->serial_stop();
                return;
            }
            if (mode == VIEW_ACTIONS) mode = VIEW_TERMINAL;
            else mode = VIEW_ACTIONS;
            selected = 0;
            render_view(mode, &state, selected);
            continue;
        }

        if (mode == VIEW_TERMINAL) {
            if (event.type == T5_UI_EVENT_PREVIOUS) {
                ++scroll_from_bottom;
                render_terminal(&state);
            } else if (event.type == T5_UI_EVENT_NEXT) {
                if (scroll_from_bottom > 0) --scroll_from_bottom;
                render_terminal(&state);
            } else if (event.type == T5_UI_EVENT_CONFIRM) {
                mode = VIEW_ACTIONS;
                selected = 0;
                render_actions(&state, selected);
            }
            continue;
        }

        uint32_t item_count = 0;
        switch (mode) {
            case VIEW_ACTIONS: item_count = 7u; break;
            case VIEW_BAUD: item_count = 12u; break;
            case VIEW_DATA_BITS: item_count = 4u; break;
            case VIEW_PARITY: item_count = 5u; break;
            case VIEW_STOP_BITS: item_count = 2u; break;
            default: break;
        }

        bool activate = false;
        if (event.type == T5_UI_EVENT_PREVIOUS) {
            selected = ui->previous_index(selected, item_count);
            render_view(mode, &state, selected);
            continue;
        }
        if (event.type == T5_UI_EVENT_NEXT) {
            selected = ui->next_index(selected, item_count);
            render_view(mode, &state, selected);
            continue;
        }
        if (event.type == T5_UI_EVENT_TAP) {
            int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit >= 0 && (uint32_t)hit < item_count) {
                selected = hit;
                activate = true;
            }
        } else if (event.type == T5_UI_EVENT_CONFIRM) {
            activate = true;
        }
        if (!activate) continue;

        if (mode == VIEW_ACTIONS) {
            if (selected == 0) {
                if (state.status == T5_USB_STATUS_READY &&
                    system_ui->keyboard_request("Send over USB serial", "", SEND_CAP - 1u,
                                                T5_SYSTEM_KEYBOARD_TEXT, COOKIE_SEND)) return;
            } else if (selected == 1) {
                mode = VIEW_BAUD;
                selected = baud_index(state.line_coding.baud_rate);
                render_baud(selected);
            } else if (selected == 2) {
                mode = VIEW_DATA_BITS;
                selected = state.line_coding.data_bits >= 5u ? (int32_t)state.line_coding.data_bits - 5 : 3;
                render_data_bits(selected);
            } else if (selected == 3) {
                mode = VIEW_PARITY;
                selected = state.line_coding.parity <= T5_USB_PARITY_SPACE ? (int32_t)state.line_coding.parity : 0;
                render_parity(selected);
            } else if (selected == 4) {
                mode = VIEW_STOP_BITS;
                selected = state.line_coding.stop_bits == 2u ? 1 : 0;
                render_stop_bits(selected);
            } else if (selected == 5) {
                if (usb->serial_set_control_lines(!state.dtr, state.rts)) state.dtr = !state.dtr;
                render_actions(&state, selected);
            } else if (selected == 6) {
                if (usb->serial_set_control_lines(state.dtr, !state.rts)) state.rts = !state.rts;
                render_actions(&state, selected);
            }
        } else if (mode == VIEW_BAUD) {
            if ((size_t)selected < sizeof(baud_rates) / sizeof(baud_rates[0])) {
                coding = state.line_coding;
                coding.baud_rate = baud_rates[selected];
                (void)apply_coding(&state, coding);
                mode = VIEW_ACTIONS;
                selected = 1;
                render_actions(&state, selected);
            } else {
                char current[16];
                snprintf(current, sizeof(current), "%lu", (unsigned long)state.line_coding.baud_rate);
                if (system_ui->keyboard_request("Custom baud rate", current, 7u,
                                                T5_SYSTEM_KEYBOARD_TEXT, COOKIE_CUSTOM_BAUD)) return;
            }
        } else if (mode == VIEW_DATA_BITS) {
            coding = state.line_coding;
            coding.data_bits = (uint8_t)(5 + selected);
            (void)apply_coding(&state, coding);
            mode = VIEW_ACTIONS;
            selected = 2;
            render_actions(&state, selected);
        } else if (mode == VIEW_PARITY) {
            coding = state.line_coding;
            coding.parity = (uint8_t)selected;
            (void)apply_coding(&state, coding);
            mode = VIEW_ACTIONS;
            selected = 3;
            render_actions(&state, selected);
        } else if (mode == VIEW_STOP_BITS) {
            coding = state.line_coding;
            coding.stop_bits = selected == 1 ? 2u : 1u;
            (void)apply_coding(&state, coding);
            mode = VIEW_ACTIONS;
            selected = 4;
            render_actions(&state, selected);
        }
    }
    usb->serial_stop();
}
