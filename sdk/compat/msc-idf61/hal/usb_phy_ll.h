#pragma once
/* Build-only IDF 4.4 -> 6.1 spelling adapter for the unchanged MSC transport.
 * All four operations have the same ESP32-S3 register effects. RCC calls use
 * the private inline forms to preserve this provider's existing exclusive-PHY
 * ownership model, without introducing RTOS locks or native firmware imports.
 */
#include <hal/usb_wrap_ll.h>
#define usb_phy_ll_usb_wrap_enable_bus_clock _usb_wrap_ll_enable_bus_clock
#define usb_phy_ll_usb_wrap_reset_register _usb_wrap_ll_reset_register
#define usb_phy_ll_usb_wrap_pad_enable usb_wrap_ll_phy_enable_pad
static inline void usb_phy_ll_int_otg_enable(usb_wrap_dev_t *hw)
{
    usb_wrap_ll_phy_enable_external(hw, false);
}
