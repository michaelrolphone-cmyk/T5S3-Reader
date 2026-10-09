#pragma once
#include <usb/usb_host.h>
typedef void*usb_phy_handle_t;
struct usb_phy_config_t{int controller,target,otg_mode,otg_speed;};
enum{USB_PHY_CTRL_OTG,USB_PHY_TARGET_INT,USB_OTG_MODE_HOST,USB_PHY_SPEED_UNDEFINED,USB_PHY_ACTION_HOST_FORCE_DISCONN,USB_PHY_ACTION_HOST_ALLOW_CONN};
esp_err_t usb_new_phy(const usb_phy_config_t*,usb_phy_handle_t*);
esp_err_t usb_del_phy(usb_phy_handle_t);
esp_err_t usb_phy_action(usb_phy_handle_t,int);
