#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_BATTERY_API_VERSION 1u
#define T5_BATTERY_BOARD_NAME_MAX 32u

typedef enum {
    T5_BATTERY_CHARGE_NOT_CHARGING = 0,
    T5_BATTERY_CHARGE_PRECHARGE = 1,
    T5_BATTERY_CHARGE_FAST = 2,
    T5_BATTERY_CHARGE_DONE = 3,
    T5_BATTERY_CHARGE_UNKNOWN = 0xFF,
} t5_battery_charge_status_t;

typedef enum {
    T5_BATTERY_GAUGE_SLEEP = 0,
    T5_BATTERY_GAUGE_FULL = 1,
    T5_BATTERY_GAUGE_CHARGE = 2,
    T5_BATTERY_GAUGE_DISCHARGE = 3,
    T5_BATTERY_GAUGE_RELAX = 4,
    T5_BATTERY_GAUGE_UNKNOWN = 0xFF,
} t5_battery_gauge_state_t;

typedef struct {
    char board_name[T5_BATTERY_BOARD_NAME_MAX];
    uint8_t available;
    uint8_t detailed_telemetry;
    uint8_t charger_ready;
    uint8_t gauge_ready;
    uint8_t charger_read_ok;
    uint8_t gauge_read_ok;
    uint8_t vbus_connected;
    uint8_t charge_enabled;
    uint8_t charging;
    uint8_t charge_done;
    uint8_t gauge_battery_full_flag;
    uint8_t gauge_gauging_full_flag;
    uint8_t gauge_taper_flag;
    uint8_t gauge_charge_inhibit;
    uint8_t charger_vbus_status;
    uint8_t charger_status;
    uint8_t gauge_state;
    uint8_t reserved0;
    uint16_t profile_capacity_mah;
    uint16_t input_limit_ma;
    uint16_t charge_current_ma;
    uint16_t precharge_current_ma;
    uint16_t termination_current_ma;
    uint16_t charger_adc_current_ma;
    uint16_t charge_voltage_mv;
    uint16_t system_voltage_mv;
    uint16_t battery_voltage_mv;
    uint16_t vbus_voltage_mv;
    uint16_t gauge_voltage_mv;
    uint16_t gauge_charge_voltage_mv;
    uint16_t gauge_taper_current_ma;
    uint16_t soc_percent;
    uint16_t soh_percent;
    uint16_t full_capacity_mah;
    uint16_t remaining_capacity_mah;
    uint16_t temperature_dk;
    uint16_t battery_status_raw;
    uint16_t gauging_status_raw;
    int16_t current_ma;
    int16_t average_current_ma;
} t5_battery_state_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    // Firmware owns battery/charger initialization, I2C access and board-specific
    // telemetry. The ELF receives only a copied snapshot.
    bool (*read)(t5_battery_state_t *state);
} t5_battery_api_v1;

const t5_battery_api_v1 *t5_battery_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
