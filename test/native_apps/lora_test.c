#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5LoRaApi.h"
#include "T5UiApi.h"

void app_main(void);

static int renders;
static int events;
static int packets_polled;
static int prepares;
static int finishes;
static int transmits;
static bool started;
static bool stopped;
static uint32_t rx_count;
static uint32_t tx_count;

static bool lora_supported(void) { return true; }
static void lora_default_config(t5_lora_config_t *config) {
    assert(config);
    memset(config, 0, sizeof(*config));
    config->frequency_hz = 915000000u;
    config->bandwidth_hz = 125000u;
    config->preamble_symbols = 8;
    config->spreading_factor = 10;
    config->coding_rate = 5;
    config->sync_word = 0x12;
    config->tx_power_dbm = 22;
    config->crc_enabled = 1;
}
static bool lora_start(const t5_lora_config_t *config) {
    assert(config);
    assert(config->frequency_hz == 915000000u);
    assert(config->bandwidth_hz == 125000u);
    assert(config->spreading_factor == 7u);
    assert(config->coding_rate == 5u);
    assert(config->sync_word == 0x12u);
    assert(config->preamble_symbols == 8u);
    assert(config->crc_enabled == 1u);
    started = true;
    return true;
}
static void lora_stop(void) { stopped = true; }
static bool lora_read_state(t5_lora_state_t *state) {
    assert(state);
    memset(state, 0, sizeof(*state));
    state->status = T5_LORA_STATUS_READY;
    state->receiver_active = 1;
    state->packets_received = rx_count;
    state->packets_sent = tx_count;
    state->config.frequency_hz = 915000000u;
    state->config.bandwidth_hz = 125000u;
    state->config.preamble_symbols = 8u;
    state->config.spreading_factor = 7u;
    state->config.coding_rate = 5u;
    state->config.sync_word = 0x12u;
    state->config.tx_power_dbm = 22;
    state->config.crc_enabled = 1u;
    return true;
}
static bool lora_poll_packet(t5_lora_packet_t *packet) {
    assert(packet);
    memset(packet, 0, sizeof(*packet));
    ++packets_polled;
    if (packets_polled != 2) return false;
    static const char message[] = "hello lora";
    packet->length = (uint16_t)(sizeof(message) - 1u);
    memcpy(packet->data, message, sizeof(message) - 1u);
    packet->rssi_dbm_x10 = -735;
    packet->snr_db_x10 = 82;
    rx_count = 1;
    return true;
}
static bool lora_transmit(const uint8_t *data, uint16_t length) {
    static const uint8_t expected[] = {'T','5','S','3',' ','p','i','n','g'};
    assert(data && length == sizeof(expected));
    assert(memcmp(data, expected, sizeof(expected)) == 0);
    ++transmits;
    ++tx_count;
    return true;
}
static bool lora_prepare_display(void) { ++prepares; return true; }
static bool lora_finish_display(void) { ++finishes; return true; }
static const t5_lora_api_v1 lora_api = {
    .api_version = T5_LORA_API_VERSION,
    .struct_size = sizeof(t5_lora_api_v1),
    .supported = lora_supported,
    .default_config = lora_default_config,
    .start = lora_start,
    .stop = lora_stop,
    .read_state = lora_read_state,
    .poll_packet = lora_poll_packet,
    .transmit = lora_transmit,
    .prepare_display = lora_prepare_display,
    .finish_display = lora_finish_display,
};
const t5_lora_api_v1 *t5_lora_get_api(uint32_t version) {
    return version == T5_LORA_API_VERSION ? &lora_api : NULL;
}

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows,
                        uint32_t row_count, int32_t selected_index) {
    assert(chrome && rows && row_count == 7 && selected_index == 0);
    assert(strcmp(chrome->title, "LoRa") == 0);
    assert(strcmp(rows[1].value, "915.000 MHz BW125 SF7 CR4/5 SW0x12") == 0);
    if (renders == 0) {
        assert(strcmp(rows[2].value, "Waiting for packet") == 0);
        assert(strcmp(rows[3].value, "--") == 0);
        assert(strcmp(rows[4].value, "--") == 0);
        assert(strcmp(rows[6].value, "RX 0 / TX 0") == 0);
    } else if (renders == 1) {
        assert(strcmp(rows[6].value, "RX 0 / TX 1") == 0);
    } else {
        assert(strcmp(rows[2].value, "10 bytes") == 0);
        assert(strcmp(rows[3].value, "68 65 6C 6C 6F 20 6C 6F 72 61") == 0);
        assert(strcmp(rows[4].value, "hello lora") == 0);
        assert(strcmp(rows[5].value, "RSSI -73 dBm / SNR 8 dB") == 0);
        assert(strcmp(rows[6].value, "RX 1 / TX 1") == 0);
    }
    ++renders;
}
static bool poll_event(t5_ui_event_t *event, uint32_t wait_ms) {
    assert(event && wait_ms == 100);
    memset(event, 0, sizeof(*event));
    ++events;
    if (events == 1) event->type = T5_UI_EVENT_CONFIRM;
    else if (events >= 3) event->type = T5_UI_EVENT_BACK;
    return true;
}
static const t5_ui_api_v1 ui_api = {
    .api_version = T5_UI_API_VERSION,
    .struct_size = sizeof(t5_ui_api_v1),
    .render_list = render_list,
    .poll_event = poll_event,
};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version) {
    return version == T5_UI_API_VERSION ? &ui_api : NULL;
}

int main(void) {
    app_main();
    assert(started);
    assert(stopped);
    assert(transmits == 1);
    assert(renders == 3);
    assert(prepares == 3);
    assert(finishes == 3);
    assert(rx_count == 1 && tx_count == 1);
    return 0;
}
