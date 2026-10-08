#pragma once
#include "usb_bounded_rig.h"
#include "BoundedAdmission.h"
#ifdef __cplusplus
extern "C" {
#endif
void admission_init(void);
esp_err_t admission_prepare(unsigned fail_at);
esp_err_t admission_dispose(void);
unsigned admission_allocations(void);
unsigned admission_live_allocations(void);
unsigned admission_channels(void);
void admission_channel_occupancy(unsigned mask);
void admission_config(unsigned interfaces,unsigned endpoints);
void admission_config_corrupt(unsigned offset,unsigned value);
void admission_hcd_fault(uint64_t token,unsigned endpoint,unsigned fault,bool enabled);
void admission_host_fault(uint64_t token,unsigned endpoint,unsigned fault,bool enabled);
void admission_device_state(unsigned state);
unsigned admission_device_refs(void);
unsigned admission_device_actions(void);
bool admission_device_opened(void);
void admission_endpoint_callback(uint64_t token,unsigned endpoint,bool stale);
uint32_t admission_endpoint_cookie(uint64_t token,unsigned endpoint);
void admission_saved_callback(uint32_t cookie);
esp_err_t admission_legacy_release(uint64_t token);
esp_err_t admission_legacy_pipe_free(uint64_t token,unsigned endpoint);
void admission_bulk_irq(uint64_t token,unsigned endpoint,unsigned event,int bytes);
#ifdef __cplusplus
}
#endif
