#pragma once
#include "usb_admission_rig.h"
#include "BoundedControl.h"
#ifdef __cplusplus
extern "C" {
#endif
void control_init(void);
void control_irq(unsigned event,int bytes);
void control_dispatch_hook(void (*hook)(unsigned));
void control_dispatch_decoded(unsigned event);
unsigned control_dispatch_count(void);
void control_dispatch_exhaust(void);
void control_pool_dispatch(uint64_t token,unsigned index);
void control_pool_drain(uint64_t token,unsigned index);
void control_exhaust_cookies(void);
unsigned control_stage(void);
bool control_direction(void);
unsigned control_pid(void);
unsigned control_count(void);
unsigned control_logs_count(void);
unsigned control_notifications_count(void);
bool control_callback_restored(void);
bool control_retired(void);
uint32_t control_cookie(void);
void control_saved_callback(uint32_t cookie);
void control_fault(unsigned fault,bool enabled);
esp_err_t control_legacy_submit(void);
void control_legacy_notify(void);
void control_legacy_updates(bool blocked);
#ifdef __cplusplus
}
#endif
