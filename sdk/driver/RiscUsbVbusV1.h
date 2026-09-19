#pragma once
/* Provider-to-provider board power API, never part of the generic RiscRTE
 * core. Board ELF arbitrates charger, external input, source current and OTG.
 * A successful release means it has verified that it is no longer sourcing;
 * external VBUS may still be present. False retains the power lease and
 * prevents the controller or board-power ELF from unmapping. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_VBUS_API_V1 1u
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    /* Only one live source lease. Reject unknown electrical/role state,
     * external VBUS, and requests above the board-verified current limit.
     * False MUST set *lease to zero; a partially applied power transition
     * must retain an internal lease and make provider quiesce return false
     * until a verified recovery has completed. */
    bool (*acquire_host)(void *context, uint32_t max_milliamps, uint64_t *lease);
    /* Source-off plus restored board state must be verified by hardware
     * readback. On failure retain ownership for explicit recovery/retry. */
    bool (*release_host)(void *context, uint64_t lease);
    /* True only when no source lease, pending rail transition, DMA/callback
     * into this provider, or lower-device claim can outlive the ELF. */
    bool (*quiesce)(void *context);
} risc_usb_vbus_api_v1;

/* An append-only extension of the SAME physical BQ25896 owner, not another
 * provider that can claim address 0x6B. Consumers check base.struct_size
 * before using extension members. Register values are copied as raw snapshots:
 * ADC conversions may be stale, and REG0C is deliberately not read because
 * that read clears latched fault history needed by VBUS fault handling. */
typedef struct {
    uint8_t input_control;       /* REG00 */
    uint8_t adc_control;         /* REG02 */
    uint8_t power_control;       /* REG03 */
    uint8_t charge_current;      /* REG04 */
    uint8_t precharge_termination; /* REG05 */
    uint8_t charge_voltage;      /* REG06 */
    uint8_t charge_timer;        /* REG07 */
    uint8_t system_status;       /* REG0B */
    uint8_t battery_adc;         /* REG0E */
    uint8_t system_adc;          /* REG0F */
    uint8_t vbus_adc;            /* REG11 */
} risc_bq25896_charger_snapshot_v1;

typedef struct {
    risc_usb_vbus_api_v1 base;  /* Must remain the first member. */
    bool (*read_charger)(void *context, risc_bq25896_charger_snapshot_v1 *out);
    /* Board-qualified 1000mA input/512mA charge/4208mV profile, applied by
     * the single chip owner through its existing I2C claim. Reject during OTG
     * and verify register readbacks. A partially applied or unverified write
     * quarantines the provider and retains its device claim until recovery;
     * callers must NOT fall back to a firmware BQ register writer. */
    bool (*configure_charger)(void *context);
    /* One-way sleep/shutdown command. Requires no host power lease, no OTG,
     * no external input and no uncertain charger state. True means the
     * BATFET_DIS command was ACKed, NOT that power-off was observed. Once
     * BATFET_DIS may have been written, the ELF must stay pinned until reboot
     * because I2C and readback may disappear with its own power. */
    bool (*request_shutdown)(void *context);
} risc_usb_vbus_charger_api_v1;
#ifdef __cplusplus
}
#endif
