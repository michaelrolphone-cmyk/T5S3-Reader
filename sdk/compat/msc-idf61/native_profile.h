#pragma once
#include <esp_idf_version.h>
#include <soc/soc_caps.h>
#include <soc/usb_pins.h>
#include <soc/io_mux_reg.h>
#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6, 1, 0)
#error "Unreviewed MSC native SDK/target"
#endif
_Static_assert(USBPHY_DM_NUM == 19 && USBPHY_DP_NUM == 20, "MSC USB-only pads");
_Static_assert(IO_MUX_GPIO19_REG == IO_MUX_GPIO0_REG + 19 * 4, "DM pad map");
_Static_assert(IO_MUX_GPIO20_REG == IO_MUX_GPIO0_REG + 20 * 4, "DP pad map");
/* IDF6 removed this capability spelling. The pinned S3 gpio_ll implementation
 * still writes the same digital IO_MUX drive field for USB-only GPIO19/20.
 * Keep the unchanged source's platform guard; never define this for another SoC.
 */
#ifndef SOC_GPIO_SUPPORT_RTC_INDEPENDENT
#define SOC_GPIO_SUPPORT_RTC_INDEPENDENT 1
#endif
