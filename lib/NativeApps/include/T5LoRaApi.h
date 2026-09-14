#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_LORA_API_VERSION 1u
#define T5_LORA_MAX_PACKET 255u

typedef enum {
    T5_LORA_STATUS_UNSUPPORTED = 0,
    T5_LORA_STATUS_OFF = 1,
    T5_LORA_STATUS_READY = 2,
    T5_LORA_STATUS_ERROR = 3,
} t5_lora_status_t;

typedef struct {
    uint32_t frequency_hz;
    uint32_t bandwidth_hz;
    uint16_t preamble_symbols;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    uint8_t sync_word;
    int8_t tx_power_dbm;
    uint8_t crc_enabled;
    uint8_t reserved[3];
} t5_lora_config_t;

typedef struct {
    uint8_t status;
    uint8_t receiver_active;
    int16_t last_error;
    uint32_t packets_received;
    uint32_t packets_sent;
    t5_lora_config_t config;
} t5_lora_state_t;

typedef struct {
    uint8_t data[T5_LORA_MAX_PACKET];
    uint16_t length;
    int16_t rssi_dbm_x10;
    int16_t snr_db_x10;
    int32_t frequency_error_hz;
    uint32_t received_at_ms;
} t5_lora_packet_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    bool (*supported)(void);
    void (*default_config)(t5_lora_config_t *config);
    bool (*start)(const t5_lora_config_t *config);
    void (*stop)(void);
    bool (*read_state)(t5_lora_state_t *state);
    bool (*poll_packet)(t5_lora_packet_t *packet);
    bool (*transmit)(const uint8_t *data, uint16_t length);

    // The T5S3 Pro multiplexes SX1262 signals with the e-paper/SD buses.
    // Native apps never manipulate those pins directly. Bracket a firmware UI
    // refresh with these calls so firmware safely powers the radio down, gives
    // the shared pins to the display, then restores the configured receiver.
    bool (*prepare_display)(void);
    bool (*finish_display)(void);
} t5_lora_api_v1;

const t5_lora_api_v1 *t5_lora_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
