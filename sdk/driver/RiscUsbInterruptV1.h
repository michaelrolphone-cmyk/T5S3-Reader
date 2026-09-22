#pragma once
#include "RiscUsbControllerV1.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The original usb.controller@1 and usb.host@1 structs remain prefix-compatible
 * with deployed CDC/serial ELFs. The extended struct_size, not a firmware
 * dispatch or separate competing controller, signals interrupt support. */
typedef struct {
    risc_usb_controller_api_v1 controller;
    int32_t (*interrupt_read)(void *context, uint64_t physical_claim,
                              uint8_t endpoint, uint8_t *dst,
                              size_t capacity, uint32_t timeout_ms);
} risc_usb_controller_interrupt_v1;
typedef struct {
    risc_usb_host_discovery_v1 discovery;
    int32_t (*interrupt_read)(void *context, uint64_t claim,
                              uint8_t endpoint, uint8_t *dst,
                              size_t capacity, uint32_t timeout_ms);
} risc_usb_host_interrupt_v1;
#ifdef __cplusplus
}
#endif
