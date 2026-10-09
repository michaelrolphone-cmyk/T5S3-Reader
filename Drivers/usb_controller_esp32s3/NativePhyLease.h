#pragma once
#include "RiscUsbPhyResourceV1.h"

namespace RiscUsbController {
// Selected only by the explicit native-PHY controller build. The native port
// owns console exclusion and lifecycle barriers; this ELF still owns USB.
class NativePhyLease {
 public:
  static bool valid(const risc_usb_phy_resource_api_v1 *api) {
    return api && api->api_version == RISC_USB_PHY_RESOURCE_API_V1 &&
      api->struct_size >= sizeof(*api) &&
      api->controller_kind == RISC_USB_PHY_ESP32S3_OTG && !api->reserved &&
      api->is_owner && api->claim && api->release;
  }
  bool bind(const risc_usb_phy_resource_api_v1 *api) {
    if (token_ || !valid(api)) return false;
    api_ = api;
    return true;
  }
  bool claim() {
    if (!api_ || token_ || !api_->is_owner(api_->context)) return false;
    // Copy a partial-claim token even on false; cleanup must retain this ELF
    // and retry release before the native console/lifecycle can resume.
    uint64_t token = 0;
    const bool ok = api_->claim(api_->context, &token);
    token_ = token;
    return ok && token_ && api_->is_owner(api_->context);
  }
  bool release() {
    if (!token_) return true;
    if (!api_ || !api_->is_owner(api_->context) ||
        !api_->release(api_->context, token_)) return false;
    token_ = 0;
    return true;
  }
  bool held() const { return token_ != 0; }
  bool unbind() {
    if (token_) return false;
    api_ = nullptr;
    return true;
  }
 private:
  const risc_usb_phy_resource_api_v1 *api_ = nullptr;
  uint64_t token_ = 0;
};
} // namespace RiscUsbController
