#include "T5AppApi.h"
#include "T5LoRaApi.h"
#include "T5UiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static const t5_lora_api_v1 *lora;
static const t5_ui_api_v1 *ui;

static char status_value[32];
static char radio_value[64];
static char packet_info_value[48] = "Waiting for packet";
static char packet_hex_value[769] = "--";
static char packet_ascii_value[257] = "--";
static char signal_value[48] = "--";
static char counters_value[48];
static char footer[96];

static void copy_text(char *dst, size_t capacity, const char *src) {
    if (!dst || capacity == 0u) return;
    size_t i = 0u;
    if (src) {
        while (src[i] && i + 1u < capacity) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static const char *status_name(uint8_t status) {
    switch (status) {
        case T5_LORA_STATUS_READY: return "Ready";
        case T5_LORA_STATUS_OFF: return "Off";
        case T5_LORA_STATUS_ERROR: return "Error";
        default: return "Unsupported";
    }
}

static char hex_digit(uint8_t value) {
    value &= 0x0fu;
    return (char)(value < 10u ? ('0' + value) : ('A' + (value - 10u)));
}

static void packet_to_raw_views(const t5_lora_packet_t *packet) {
    if (!packet || packet->length == 0u) return;

    const size_t max_hex_bytes = (sizeof(packet_hex_value) - 1u) / 3u;
    const size_t max_ascii_bytes = sizeof(packet_ascii_value) - 1u;
    size_t count = (size_t)packet->length;
    if (count > max_hex_bytes) count = max_hex_bytes;
    if (count > max_ascii_bytes) count = max_ascii_bytes;

    size_t hex_pos = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const uint8_t byte = packet->data[i];
        packet_hex_value[hex_pos++] = hex_digit((uint8_t)(byte >> 4));
        packet_hex_value[hex_pos++] = hex_digit(byte);
        if (i + 1u < count) packet_hex_value[hex_pos++] = ' ';

        packet_ascii_value[i] = (byte >= 32u && byte <= 126u) ? (char)byte : '.';
    }
    packet_hex_value[hex_pos] = '\0';
    packet_ascii_value[count] = '\0';

    snprintf(packet_info_value, sizeof(packet_info_value), "%u byte%s",
             (unsigned)packet->length, packet->length == 1u ? "" : "s");
    snprintf(signal_value, sizeof(signal_value), "RSSI %d dBm / SNR %d dB",
             (int)(packet->rssi_dbm_x10 / 10), (int)(packet->snr_db_x10 / 10));
}

static void render_state(const t5_lora_state_t *state) {
    copy_text(status_value, sizeof(status_value), status_name(state->status));
    snprintf(radio_value, sizeof(radio_value), "%lu.%03lu MHz  BW%lu  SF%u",
             (unsigned long)(state->config.frequency_hz / 1000000u),
             (unsigned long)((state->config.frequency_hz / 1000u) % 1000u),
             (unsigned long)(state->config.bandwidth_hz / 1000u),
             (unsigned)state->config.spreading_factor);
    snprintf(counters_value, sizeof(counters_value), "RX %lu / TX %lu",
             (unsigned long)state->packets_received, (unsigned long)state->packets_sent);

    if (state->status == T5_LORA_STATUS_UNSUPPORTED) {
        copy_text(footer, sizeof(footer), "SX1262 hardware is not available on this board");
    } else if (state->status == T5_LORA_STATUS_ERROR) {
        snprintf(footer, sizeof(footer), "Radio error %d", (int)state->last_error);
    } else {
        copy_text(footer, sizeof(footer), "Confirm sends a T5S3 ping");
    }

    const t5_ui_chrome_t chrome = {
        .title = "LoRa",
        .subtitle = "SX1262 radio",
        .status = footer,
        .back_label = "Back",
        .confirm_label = "Ping",
        .previous_label = "",
        .next_label = "",
    };
    const t5_ui_list_row_t rows[] = {
        {.title = "Status", .subtitle = NULL, .value = status_value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "Radio", .subtitle = NULL, .value = radio_value, .flags = 0},
        {.title = "Last RX", .subtitle = NULL, .value = packet_info_value, .flags = T5_UI_LIST_HIGHLIGHT_VALUE},
        {.title = "HEX", .subtitle = NULL, .value = packet_hex_value, .flags = 0},
        {.title = "ASCII", .subtitle = NULL, .value = packet_ascii_value, .flags = 0},
        {.title = "Signal", .subtitle = NULL, .value = signal_value, .flags = 0},
        {.title = "Packets", .subtitle = NULL, .value = counters_value, .flags = 0},
    };

    // Firmware owns the shared display/radio pin arbitration. The app only
    // brackets its themed UI refresh with the LoRa service calls.
    if (lora->prepare_display) (void)lora->prepare_display();
    ui->render_list(&chrome, rows, (uint32_t)(sizeof(rows) / sizeof(rows[0])), 0);
    if (lora->finish_display) (void)lora->finish_display();
}

void app_main(void) {
    lora = t5_lora_get_api(T5_LORA_API_VERSION);
    ui = t5_ui_get_api(T5_UI_API_VERSION);
    if (!lora || !ui || !lora->supported || !lora->default_config || !lora->start || !lora->stop ||
        !lora->read_state || !lora->poll_packet || !lora->transmit || !ui->render_list || !ui->poll_event) return;

    t5_lora_config_t config = {0};
    t5_lora_state_t state = {0};
    t5_lora_packet_t packet = {0};
    lora->default_config(&config);

    if (!lora->supported()) {
        state.status = T5_LORA_STATUS_UNSUPPORTED;
        state.config = config;
    } else {
        (void)lora->start(&config);
        (void)lora->read_state(&state);
    }
    render_state(&state);
    (void)lora->read_state(&state);

    for (;;) {
        if (lora->poll_packet(&packet)) {
            packet_to_raw_views(&packet);
            (void)lora->read_state(&state);
            render_state(&state);
            (void)lora->read_state(&state);
        }

        t5_ui_event_t event = {0};
        if (!ui->poll_event(&event, 100)) break;
        if (event.type == T5_UI_EVENT_EXIT || event.type == T5_UI_EVENT_BACK) break;
        if (event.type == T5_UI_EVENT_CONFIRM && state.status == T5_LORA_STATUS_READY) {
            static const uint8_t ping[] = {'T','5','S','3',' ','p','i','n','g'};
            (void)lora->transmit(ping, (uint16_t)sizeof(ping));
            (void)lora->read_state(&state);
            render_state(&state);
            (void)lora->read_state(&state);
        }
    }

    lora->stop();
}
