#include "T5AppApi.h"
#include "T5StreamApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"
#include "T5UsbApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TERMINAL_CAP 12288u
#define SEND_CAP 257u
#define DETECT_SAMPLE_CAP 320u
#define DETECT_SETTLE_TICKS 2u
#define DETECT_SAMPLE_TICKS 5u
#define DETECT_CONFIG_TICKS 12u
#define DETECT_ACCEPT_SCORE 600u
#define COOKIE_SEND 0x55534201u
#define COOKIE_CUSTOM_BAUD 0x55534202u

static const t5_usb_api_v1 *usb;
static const t5_stream_api_v1 *streams;
static const t5_ui_api_v1 *ui;
static const t5_system_ui_api_v1 *system_ui;
static t5_stream_t serial_stream;
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
    VIEW_AUTODETECT,
} view_mode_t;

typedef struct {
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
} detect_format_t;

typedef struct {
    t5_usb_line_coding_t coding;
    uint16_t score;
    uint16_t bytes;
} detect_result_t;

typedef enum {
    DETECT_NONE = 0,
    DETECT_BAUD,
    DETECT_FORMAT,
} detect_phase_t;

static const uint32_t baud_rates[] = {
    1200u, 2400u, 4800u, 9600u, 19200u, 38400u,
    57600u, 115200u, 230400u, 460800u, 921600u,
};

static const uint32_t detect_bauds[] = {
    115200u, 9600u, 38400u, 57600u, 19200u, 74880u,
    230400u, 460800u, 921600u, 14400u, 28800u,
    4800u, 2400u, 1200u, 600u, 300u,
};

static const detect_format_t detect_formats[] = {
    {8u, T5_USB_PARITY_NONE, 1u},
    {8u, T5_USB_PARITY_EVEN, 1u},
    {8u, T5_USB_PARITY_ODD, 1u},
    {8u, T5_USB_PARITY_MARK, 1u},
    {8u, T5_USB_PARITY_SPACE, 1u},
    {7u, T5_USB_PARITY_NONE, 1u},
    {7u, T5_USB_PARITY_EVEN, 1u},
    {7u, T5_USB_PARITY_ODD, 1u},
    {8u, T5_USB_PARITY_NONE, 2u},
    {8u, T5_USB_PARITY_EVEN, 2u},
    {8u, T5_USB_PARITY_ODD, 2u},
    {7u, T5_USB_PARITY_NONE, 2u},
    {7u, T5_USB_PARITY_EVEN, 2u},
    {7u, T5_USB_PARITY_ODD, 2u},
};

static detect_phase_t detect_phase;
static t5_usb_line_coding_t detect_original;
static t5_usb_line_coding_t detect_candidate;
static detect_result_t detect_best;
static detect_result_t detect_runner_up;
static uint8_t detect_sample[DETECT_SAMPLE_CAP];
static size_t detect_sample_len;
static size_t detect_index;
static size_t detect_format_baud_slot;
static size_t detect_format_baud_count;
static uint32_t detect_top_bauds[3];
static uint16_t detect_top_scores[3];
static uint16_t detect_top_bytes[3];
static uint8_t detect_settle_ticks;
static uint8_t detect_sample_ticks;
static uint8_t detect_config_ticks;
static bool detect_candidate_active;
static bool detect_seen_bytes;
static int detect_outcome;

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

static bool stream_api_valid(const t5_stream_api_v1 *api) {
    const size_t required = offsetof(t5_stream_api_v1, close) + sizeof(api->close);
    return api && api->struct_size >= required && api->open_usb && api->read && api->write && api->close;
}

static bool open_serial_stream(void) {
    if (serial_stream) return true;
    if (!streams) return false;
    return streams->open_usb(&serial_stream) == T5_STREAM_OK && serial_stream != 0;
}

static void close_serial_stream(void) {
    if (serial_stream && streams && streams->close) (void)streams->close(serial_stream);
    serial_stream = 0;
}

static size_t read_serial_stream(uint8_t *data, size_t capacity, t5_stream_result_t *result_out) {
    uint32_t count = 0;
    t5_stream_result_t result;
    if (!serial_stream || !streams || !data || !capacity) {
        if (result_out) *result_out = T5_STREAM_INVALID;
        return 0;
    }
    if (capacity > T5_STREAM_CHUNK) capacity = T5_STREAM_CHUNK;
    result = streams->read(serial_stream, data, (uint32_t)capacity, &count);
    if (result_out) *result_out = result;
    if (result != T5_STREAM_OK) return 0;
    return (size_t)count;
}

static bool write_serial_stream(const uint8_t *data, size_t length) {
    uint32_t count = 0;
    if (!serial_stream || !streams || !data || !length || length > T5_STREAM_CHUNK) return false;
    return streams->write(serial_stream, data, (uint32_t)length, &count) == T5_STREAM_OK &&
           count == (uint32_t)length;
}

static void drain_serial_stream(void) {
    uint8_t scratch[128];
    unsigned pass;
    for (pass = 0; pass < 4u; ++pass) {
        t5_stream_result_t result = T5_STREAM_AGAIN;
        if (!read_serial_stream(scratch, sizeof(scratch), &result)) break;
    }
}

static bool coding_equal(const t5_usb_line_coding_t *a, const t5_usb_line_coding_t *b) {
    return a->baud_rate == b->baud_rate && a->data_bits == b->data_bits &&
           a->parity == b->parity && a->stop_bits == b->stop_bits;
}

static void coding_text(const t5_usb_line_coding_t *coding, char *buffer, size_t capacity) {
    snprintf(buffer, capacity, "%lu %u%c%u",
             (unsigned long)coding->baud_rate,
             (unsigned)coding->data_bits,
             parity_letter(coding->parity),
             (unsigned)coding->stop_bits);
}

static bool contains_token(const uint8_t *data, size_t length, const char *token) {
    size_t token_len = 0;
    size_t i;
    while (token[token_len]) ++token_len;
    if (!token_len || token_len > length) return false;
    for (i = 0; i + token_len <= length; ++i) {
        size_t j = 0;
        while (j < token_len && data[i + j] == (uint8_t)token[j]) ++j;
        if (j == token_len) return true;
    }
    return false;
}

static uint16_t score_sample(const uint8_t *data, size_t length) {
    uint32_t text = 0;
    uint32_t alnum = 0;
    uint32_t lines = 0;
    uint32_t weird = 0;
    uint32_t longest = 0;
    uint32_t run = 0;
    uint32_t distinct = 0;
    uint32_t score;
    uint8_t seen[32] = {0};
    size_t i;

    if (!length) return 0;
    for (i = 0; i < length; ++i) {
        uint8_t c = data[i];
        uint8_t mask = (uint8_t)(1u << (c & 7u));
        uint8_t *slot = &seen[c >> 3u];
        if (!(*slot & mask)) {
            *slot |= mask;
            ++distinct;
        }
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 32u && c <= 126u)) {
            ++text;
            ++run;
            if (run > longest) longest = run;
        } else {
            run = 0;
        }
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ++alnum;
        if (c == '\n') ++lines;
        if (c == 0u || c == 0xffu) ++weird;
    }

    score = (text * 620u) / (uint32_t)length;
    score += (alnum * 120u) / (uint32_t)length;
    score += (longest > 32u ? 32u : longest) * 5u;
    score += (lines > 4u ? 4u : lines) * 18u;
    score += (distinct > 24u ? 24u : distinct) * 4u;

    if (contains_token(data, length, "ESP-ROM") || contains_token(data, length, "MicroPython") ||
        contains_token(data, length, "rst:") || contains_token(data, length, ">>>")) {
        score += 220u;
    }
    if (contains_token(data, length, "$GP") || contains_token(data, length, "$GN") ||
        contains_token(data, length, "NMEA") || contains_token(data, length, "AT+") ||
        contains_token(data, length, "ERROR") || contains_token(data, length, "OK\r")) {
        score += 180u;
    }
    if (contains_token(data, length, "{\"") || contains_token(data, length, "\":") ||
        contains_token(data, length, "\r\n")) {
        score += 80u;
    }

    if (weird * 4u > (uint32_t)length) {
        score = score > 120u ? score - 120u : 0u;
    }
    if (distinct < 3u && length >= 8u) score /= 2u;
    if (length < 8u) score = (score * (uint32_t)length) / 8u;
    if (score > 1000u) score = 1000u;
    return (uint16_t)score;
}

static const char *confidence_name(uint16_t score) {
    if (score >= 820u) return "High";
    if (score >= 680u) return "Medium";
    return "Low";
}

static void insert_top_baud(uint32_t baud, uint16_t score, uint16_t bytes) {
    size_t i;
    for (i = 0; i < 3u; ++i) {
        if (score > detect_top_scores[i]) {
            size_t j;
            for (j = 2u; j > i; --j) {
                detect_top_bauds[j] = detect_top_bauds[j - 1u];
                detect_top_scores[j] = detect_top_scores[j - 1u];
                detect_top_bytes[j] = detect_top_bytes[j - 1u];
            }
            detect_top_bauds[i] = baud;
            detect_top_scores[i] = score;
            detect_top_bytes[i] = bytes;
            return;
        }
    }
}

static void consider_best(const t5_usb_line_coding_t *coding, uint16_t score, uint16_t bytes) {
    if (score > detect_best.score) {
        detect_runner_up = detect_best;
        detect_best.coding = *coding;
        detect_best.score = score;
        detect_best.bytes = bytes;
    } else if (score > detect_runner_up.score) {
        detect_runner_up.coding = *coding;
        detect_runner_up.score = score;
        detect_runner_up.bytes = bytes;
    }
}

static bool start_detect_candidate(t5_usb_serial_state_t *state, t5_usb_line_coding_t coding) {
    drain_serial_stream();
    detect_sample_len = 0;
    detect_settle_ticks = DETECT_SETTLE_TICKS;
    detect_sample_ticks = 0;
    detect_config_ticks = 0;
    detect_candidate = coding;
    detect_candidate_active = false;
    if (!usb->serial_set_line_coding(&coding)) return false;
    state->line_coding = coding;
    detect_candidate_active = true;
    return true;
}

static bool start_current_detect_candidate(t5_usb_serial_state_t *state) {
    t5_usb_line_coding_t coding = detect_original;
    if (detect_phase == DETECT_BAUD) {
        coding.baud_rate = detect_bauds[detect_index];
        coding.data_bits = 8u;
        coding.parity = T5_USB_PARITY_NONE;
        coding.stop_bits = 1u;
    } else if (detect_phase == DETECT_FORMAT) {
        const detect_format_t *format = &detect_formats[detect_index];
        coding.baud_rate = detect_top_bauds[detect_format_baud_slot];
        coding.data_bits = format->data_bits;
        coding.parity = format->parity;
        coding.stop_bits = format->stop_bits;
    } else {
        return false;
    }
    return start_detect_candidate(state, coding);
}

static void render_detect(const t5_usb_serial_state_t *state) {
    char candidate[32];
    char best[32];
    char body[384];
    char best_line[96];
    t5_ui_text_view_result_t result = {0};
    candidate[0] = 0;
    best[0] = 0;
    best_line[0] = 0;
    if (detect_candidate_active) coding_text(&detect_candidate, candidate, sizeof(candidate));
    if (detect_best.score) {
        coding_text(&detect_best.coding, best, sizeof(best));
        snprintf(best_line, sizeof(best_line), "%s  score %u", best, (unsigned)detect_best.score);
    } else {
        snprintf(best_line, sizeof(best_line), "None yet");
    }

    if (detect_phase == DETECT_BAUD) {
        snprintf(body, sizeof(body),
                 "Passive auto detect\n\n"
                 "Nothing is transmitted and DTR/RTS are not changed.\n\n"
                 "Scanning baud rates: %u/%u\n"
                 "Current: %s\n\n"
                 "Then the strongest baud is tested across data bits, parity, and stop bits.",
                 (unsigned)(detect_index + 1u),
                 (unsigned)(sizeof(detect_bauds) / sizeof(detect_bauds[0])),
                 candidate[0] ? candidate : "configuring");
    } else {
        snprintf(body, sizeof(body),
                 "Passive auto detect\n\n"
                 "Testing framing: baud candidate %u/%u, format %u/%u\n"
                 "Current: %s\n\n"
                 "Best so far: %s",
                 (unsigned)(detect_format_baud_slot + 1u),
                 (unsigned)detect_format_baud_count,
                 (unsigned)(detect_index + 1u),
                 (unsigned)(sizeof(detect_formats) / sizeof(detect_formats[0])),
                 candidate[0] ? candidate : "configuring",
                 best_line);
    }

    const t5_ui_chrome_t chrome = {
        .title = "USB Serial",
        .subtitle = "Auto Detect",
        .status = state->status == T5_USB_STATUS_READY ? "Listening" : status_name(state->status),
        .back_label = "Cancel",
        .confirm_label = NULL,
        .previous_label = NULL,
        .next_label = NULL,
    };
    ui->render_text_view(&chrome, body, 0, &result);
}

static void finish_detect_no_match(t5_usb_serial_state_t *state) {
    (void)usb->serial_set_line_coding(&detect_original);
    state->line_coding = detect_original;
    detect_phase = DETECT_NONE;
    detect_candidate_active = false;
    detect_outcome = 2;
}

static void finish_detect_success(t5_usb_serial_state_t *state) {
    char coding[32];
    char notice[128];
    (void)usb->serial_set_line_coding(&detect_best.coding);
    state->line_coding = detect_best.coding;
    coding_text(&detect_best.coding, coding, sizeof(coding));
    if (detect_runner_up.score && (uint32_t)detect_best.score - (uint32_t)detect_runner_up.score <= 30u) {
        snprintf(notice, sizeof(notice), "Auto detected %s; framing ambiguous, using best text match", coding);
    } else {
        snprintf(notice, sizeof(notice), "Auto detected %s (%s text confidence)", coding, confidence_name(detect_best.score));
    }
    append_notice(notice);
    detect_phase = DETECT_NONE;
    detect_candidate_active = false;
    detect_outcome = 1;
}

static void begin_format_phase(t5_usb_serial_state_t *state) {
    if (!detect_top_scores[0] || (!detect_seen_bytes && detect_top_bytes[0] == 0u)) {
        finish_detect_no_match(state);
        return;
    }
    detect_phase = DETECT_FORMAT;
    detect_index = 0;
    detect_format_baud_slot = 0;
    detect_format_baud_count = detect_top_scores[0] >= 650u ? 1u : 2u;
    if (!detect_top_bauds[1] || !detect_top_scores[1]) detect_format_baud_count = 1u;
    detect_best.score = 0;
    detect_best.bytes = 0;
    detect_runner_up.score = 0;
    detect_runner_up.bytes = 0;
    detect_candidate_active = false;
    if (!start_current_detect_candidate(state)) {
        finish_detect_no_match(state);
        return;
    }
    render_detect(state);
}

static void advance_detect_candidate(t5_usb_serial_state_t *state) {
    if (detect_phase == DETECT_BAUD) {
        ++detect_index;
        if (detect_index >= sizeof(detect_bauds) / sizeof(detect_bauds[0])) {
            begin_format_phase(state);
            return;
        }
    } else if (detect_phase == DETECT_FORMAT) {
        ++detect_index;
        if (detect_index >= sizeof(detect_formats) / sizeof(detect_formats[0])) {
            detect_index = 0;
            ++detect_format_baud_slot;
            if (detect_format_baud_slot >= detect_format_baud_count) {
                if (detect_best.score >= DETECT_ACCEPT_SCORE && detect_best.bytes >= 4u)
                    finish_detect_success(state);
                else
                    finish_detect_no_match(state);
                return;
            }
        }
    }

    if (!start_current_detect_candidate(state)) {
        detect_sample_len = 0;
        detect_sample_ticks = DETECT_SAMPLE_TICKS;
        detect_candidate_active = false;
    }
    if (detect_phase != DETECT_NONE &&
        ((detect_phase == DETECT_BAUD && (detect_index % 4u) == 0u) ||
         (detect_phase == DETECT_FORMAT && detect_index == 0u))) {
        render_detect(state);
    }
}

static void complete_detect_candidate(t5_usb_serial_state_t *state) {
    uint16_t score = score_sample(detect_sample, detect_sample_len);
    uint16_t bytes = detect_sample_len > 65535u ? 65535u : (uint16_t)detect_sample_len;
    if (bytes) detect_seen_bytes = true;

    if (detect_phase == DETECT_BAUD) {
        insert_top_baud(detect_candidate.baud_rate, score, bytes);
    } else if (detect_phase == DETECT_FORMAT) {
        consider_best(&detect_candidate, score, bytes);
    }
    advance_detect_candidate(state);
}

static void cancel_auto_detect(t5_usb_serial_state_t *state) {
    if (detect_phase == DETECT_NONE) return;
    (void)usb->serial_set_line_coding(&detect_original);
    state->line_coding = detect_original;
    detect_phase = DETECT_NONE;
    detect_candidate_active = false;
    detect_outcome = 0;
    drain_serial_stream();
    append_notice("Auto detect cancelled; previous settings restored");
}

static bool start_auto_detect(t5_usb_serial_state_t *state) {
    size_t i;
    if (!state || state->status != T5_USB_STATUS_READY || !serial_stream) return false;
    detect_original = state->line_coding;
    detect_best.score = 0;
    detect_best.bytes = 0;
    detect_runner_up.score = 0;
    detect_runner_up.bytes = 0;
    detect_phase = DETECT_BAUD;
    detect_index = 0;
    detect_format_baud_slot = 0;
    detect_format_baud_count = 0;
    detect_sample_len = 0;
    detect_seen_bytes = false;
    detect_outcome = 0;
    for (i = 0; i < 3u; ++i) {
        detect_top_bauds[i] = 0;
        detect_top_scores[i] = 0;
        detect_top_bytes[i] = 0;
    }
    if (!start_current_detect_candidate(state)) {
        detect_phase = DETECT_NONE;
        return false;
    }
    render_detect(state);
    return true;
}

static bool detection_capture_ready(const t5_usb_serial_state_t *state) {
    if (detect_phase == DETECT_NONE || !detect_candidate_active) return false;
    if (state->status != T5_USB_STATUS_READY || !coding_equal(&state->line_coding, &detect_candidate)) return false;
    return detect_settle_ticks == 0u;
}

static void detection_tick(t5_usb_serial_state_t *state, const uint8_t *incoming, size_t received, bool capture) {
    if (detect_phase == DETECT_NONE) return;

    if (!detect_candidate_active) {
        advance_detect_candidate(state);
        return;
    }

    if (state->status == T5_USB_STATUS_ERROR) {
        detect_sample_len = 0;
        complete_detect_candidate(state);
        return;
    }

    if (state->status != T5_USB_STATUS_READY || !coding_equal(&state->line_coding, &detect_candidate)) {
        if (++detect_config_ticks >= DETECT_CONFIG_TICKS) {
            detect_sample_len = 0;
            complete_detect_candidate(state);
        }
        return;
    }

    detect_config_ticks = 0;
    if (detect_settle_ticks) {
        --detect_settle_ticks;
        return;
    }

    if (capture && received) {
        size_t copy = received;
        if (copy > sizeof(detect_sample) - detect_sample_len) copy = sizeof(detect_sample) - detect_sample_len;
        for (size_t i = 0; i < copy; ++i) detect_sample[detect_sample_len + i] = incoming[i];
        detect_sample_len += copy;
    }

    ++detect_sample_ticks;
    if (detect_sample_len >= sizeof(detect_sample) || detect_sample_ticks >= DETECT_SAMPLE_TICKS)
        complete_detect_candidate(state);
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
    ui->render_text_view(&chrome, terminal_len ? terminal :
                         "Connect a USB serial device to the USB-C OTG port.\n\n"
                         "Use Actions > Auto detect to identify baud and framing passively.",
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
        {"Auto detect", NULL, state->status == T5_USB_STATUS_READY ? "Baud + framing" : "Connect first", T5_UI_LIST_HIGHLIGHT_VALUE},
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
        case VIEW_AUTODETECT: render_detect(state); break;
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

static void stop_and_close(void) {
    close_serial_stream();
    usb->serial_stop();
}

void app_main(void) {
    usb = t5_usb_get_api(T5_USB_API_VERSION);
    streams = t5_stream_get_api(T5_STREAM_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    system_ui = t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);
    if (!usb || !stream_api_valid(streams) || !ui || !system_ui || !usb->supported || !usb->serial_start ||
        !usb->serial_stop || !usb->serial_read_state || !usb->serial_set_line_coding ||
        !usb->serial_set_control_lines || !ui->render_text_view || !ui->render_list ||
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
        if (!open_serial_stream()) append_notice("Unable to open RiscRTE USB byte stream");
    }

    if (system_ui->keyboard_take_result(keyboard_text, sizeof(keyboard_text), &cancelled, &cookie) && !cancelled) {
        if (cookie == COOKIE_SEND && keyboard_text[0] && state.status == T5_USB_STATUS_READY) {
            uint8_t outgoing[SEND_CAP + 2u];
            size_t length = 0;
            while (keyboard_text[length] && length < SEND_CAP - 1u) {
                outgoing[length] = (uint8_t)keyboard_text[length];
                ++length;
            }
            outgoing[length++] = '\r';
            outgoing[length++] = '\n';
            if (write_serial_stream(outgoing, length)) {
                append_bytes((const uint8_t *)"> ", 2u);
                append_bytes((const uint8_t *)keyboard_text, length - 2u);
                append_char('\n');
            } else {
                append_notice("USB stream busy; send again");
            }
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
    bool stream_error_reported = false;
    render_view(mode, &state, selected);

    for (;;) {
        bool repaint = false;
        t5_usb_serial_state_t next = {0};
        if (usb->serial_read_state(&next)) {
            if (state_changed(&state, &next)) repaint = true;
            state = next;
        }

        uint8_t incoming[256];
        t5_stream_result_t stream_result = T5_STREAM_AGAIN;
        bool capture = mode == VIEW_AUTODETECT && detection_capture_ready(&state);
        size_t received = read_serial_stream(incoming, sizeof(incoming), &stream_result);
        if (mode == VIEW_AUTODETECT) {
            detection_tick(&state, incoming, received, capture);
            if (detect_outcome != 0) {
                if (detect_outcome == 2) append_notice("Auto detect inconclusive; previous settings restored");
                detect_outcome = 0;
                mode = VIEW_TERMINAL;
                selected = 0;
                scroll_from_bottom = 0;
                render_terminal(&state);
                repaint = false;
            }
        } else if (received) {
            append_bytes(incoming, received);
            scroll_from_bottom = 0;
            repaint = true;
            stream_error_reported = false;
        } else if (stream_result < 0 && stream_result != T5_STREAM_DISCONNECTED && !stream_error_reported) {
            append_notice("USB byte stream error");
            stream_error_reported = true;
            repaint = true;
        }

        if (repaint && (mode == VIEW_TERMINAL || mode == VIEW_ACTIONS)) render_view(mode, &state, selected);

        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 75)) break;
        if (event.type == T5_UI_EVENT_EXIT) {
            stop_and_close();
            return;
        }
        if (event.type == T5_UI_EVENT_BACK) {
            if (mode == VIEW_AUTODETECT) {
                cancel_auto_detect(&state);
                mode = VIEW_ACTIONS;
                selected = 0;
                render_actions(&state, selected);
                continue;
            }
            if (mode == VIEW_TERMINAL) {
                stop_and_close();
                return;
            }
            if (mode == VIEW_ACTIONS) mode = VIEW_TERMINAL;
            else mode = VIEW_ACTIONS;
            selected = 0;
            render_view(mode, &state, selected);
            continue;
        }

        if (mode == VIEW_AUTODETECT) continue;

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
            case VIEW_ACTIONS: item_count = 8u; break;
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
                if (state.status == T5_USB_STATUS_READY && start_auto_detect(&state)) {
                    mode = VIEW_AUTODETECT;
                    selected = 0;
                } else {
                    append_notice("Auto detect requires a connected serial device");
                    mode = VIEW_TERMINAL;
                    render_terminal(&state);
                }
            } else if (selected == 1) {
                if (state.status == T5_USB_STATUS_READY &&
                    system_ui->keyboard_request("Send over USB serial", "", SEND_CAP - 1u,
                                                T5_SYSTEM_KEYBOARD_TEXT, COOKIE_SEND)) {
                    close_serial_stream();
                    return;
                }
            } else if (selected == 2) {
                mode = VIEW_BAUD;
                selected = baud_index(state.line_coding.baud_rate);
                render_baud(selected);
            } else if (selected == 3) {
                mode = VIEW_DATA_BITS;
                selected = state.line_coding.data_bits >= 5u ? (int32_t)state.line_coding.data_bits - 5 : 3;
                render_data_bits(selected);
            } else if (selected == 4) {
                mode = VIEW_PARITY;
                selected = state.line_coding.parity <= T5_USB_PARITY_SPACE ? (int32_t)state.line_coding.parity : 0;
                render_parity(selected);
            } else if (selected == 5) {
                mode = VIEW_STOP_BITS;
                selected = state.line_coding.stop_bits == 2u ? 1 : 0;
                render_stop_bits(selected);
            } else if (selected == 6) {
                if (usb->serial_set_control_lines(!state.dtr, state.rts)) state.dtr = !state.dtr;
                render_actions(&state, selected);
            } else if (selected == 7) {
                if (usb->serial_set_control_lines(state.dtr, !state.rts)) state.rts = !state.rts;
                render_actions(&state, selected);
            }
        } else if (mode == VIEW_BAUD) {
            if ((size_t)selected < sizeof(baud_rates) / sizeof(baud_rates[0])) {
                coding = state.line_coding;
                coding.baud_rate = baud_rates[selected];
                (void)apply_coding(&state, coding);
                mode = VIEW_ACTIONS;
                selected = 2;
                render_actions(&state, selected);
            } else {
                char current[16];
                snprintf(current, sizeof(current), "%lu", (unsigned long)state.line_coding.baud_rate);
                if (system_ui->keyboard_request("Custom baud rate", current, 7u,
                                                T5_SYSTEM_KEYBOARD_TEXT, COOKIE_CUSTOM_BAUD)) {
                    close_serial_stream();
                    return;
                }
            }
        } else if (mode == VIEW_DATA_BITS) {
            coding = state.line_coding;
            coding.data_bits = (uint8_t)(5 + selected);
            (void)apply_coding(&state, coding);
            mode = VIEW_ACTIONS;
            selected = 3;
            render_actions(&state, selected);
        } else if (mode == VIEW_PARITY) {
            coding = state.line_coding;
            coding.parity = (uint8_t)selected;
            (void)apply_coding(&state, coding);
            mode = VIEW_ACTIONS;
            selected = 4;
            render_actions(&state, selected);
        } else if (mode == VIEW_STOP_BITS) {
            coding = state.line_coding;
            coding.stop_bits = selected == 1 ? 2u : 1u;
            (void)apply_coding(&state, coding);
            mode = VIEW_ACTIONS;
            selected = 5;
            render_actions(&state, selected);
        }
    }
    stop_and_close();
}
