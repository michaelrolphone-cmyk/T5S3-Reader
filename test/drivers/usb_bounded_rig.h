#pragma once
#include <usb/usb_host.h>
#ifdef __cplusplus
extern "C" {
#endif
void rig_reset(void);
usb_transfer_t *rig_transfer(void);
usb_device_handle_t rig_device(void);
usb_host_client_handle_t rig_client(void);
void rig_lock(unsigned lock, bool blocked);
void rig_library_pending(unsigned flags);
void rig_library_missing(void);
unsigned rig_library_flags(void);
void rig_irq(unsigned event, int bytes);
void rig_disconnect(void);
void rig_queue_events(unsigned count, bool replenish);
unsigned rig_events(void);
unsigned rig_halts(void);
bool rig_dma_active(void);
bool rig_halt_owned(void);
bool rig_callback_custody(void);
void rig_extra_pending(unsigned count);
void rig_pending_transfer(void);
unsigned rig_checked_flush_callbacks(void);
void rig_legacy_messages(void);
void rig_packet(unsigned packet);
void rig_direction(bool reading);
void rig_interface(unsigned number, unsigned alternate);
esp_err_t rig_legacy_command(void);
esp_err_t rig_legacy_free(void);
#ifdef __cplusplus
}
#endif
