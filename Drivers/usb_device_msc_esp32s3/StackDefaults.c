/* Resolve all optional TinyUSB weak hooks inside the ordinary ELF. No fallback
 * USB symbols may be resolved from resident firmware. */
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "device/dcd.h"
const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count) { *count=0;return NULL; }
const uint8_t *tud_descriptor_bos_cb(void) { return NULL; }
const uint8_t *tud_descriptor_device_qualifier_cb(void) { return NULL; }
const uint8_t *tud_descriptor_other_speed_configuration_cb(uint8_t index) { (void)index;return NULL; }
bool tud_vendor_control_xfer_cb(uint8_t port,uint8_t stage,const tusb_control_request_t *request) {
 (void)port;(void)stage;(void)request;return false;
}
void dcd_edpt0_status_complete(uint8_t port,const tusb_control_request_t *request) { (void)port;(void)request; }
uint8_t tud_msc_get_maxlun_cb(void) { return 1; }
void tud_msc_read10_complete_cb(uint8_t lun) { (void)lun; }
void tud_msc_write10_complete_cb(uint8_t lun) { (void)lun; }
int32_t tud_msc_request_sense_cb(uint8_t lun,void *buffer,uint16_t size) {
 (void)lun;(void)buffer; /* TinyUSB already populated its fixed sense response. */
 return size>=sizeof(scsi_sense_fixed_resp_t)?(int32_t)sizeof(scsi_sense_fixed_resp_t):-1;
}
