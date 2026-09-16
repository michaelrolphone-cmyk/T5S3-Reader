#include "UsbCdcDriverModule.h"
#include <cstring>
extern "C" {
#include <esp_dlfcn.h>
}

bool UsbCdcDriverModule::load(const char* path) {
  // A failed dlclose retains the handle. Never replace a live or uncertain
  // module or call it after its ABI validation has failed.
  if (handle_ || !path || !path[0]) return false;
  state_ = State::Failed;
  (void)dlerror();
  handle_ = dlopen(path, RTLD_NOW);
  if (!handle_) return false;
  state_ = State::Loaded;
  (void)dlerror();
  auto get = reinterpret_cast<t5_driver_get_fn>(dlsym(handle_, "t5_driver_get"));
  const char* error = dlerror();
  if (!error && get) {
    const auto* candidate = get(T5_DRIVER_ABI_VERSION);
    if (candidate && candidate->abi_version == T5_DRIVER_ABI_VERSION &&
        candidate->struct_size >= sizeof(t5_driver_v1) && candidate->driver_id &&
        std::strcmp(candidate->driver_id, "usb-cdc-acm") == 0 &&
        candidate->capability_id &&
        std::strcmp(candidate->capability_id, T5_USB_CDC_CLASS_CAPABILITY) == 0 &&
        candidate->capability_api == T5_USB_CDC_CLASS_API_VERSION &&
        candidate->capability && candidate->start && candidate->stop) {
      const auto* api = static_cast<const t5_usb_cdc_class_api_v1*>(candidate->capability);
      if (api->api_version == T5_USB_CDC_CLASS_API_VERSION &&
          api->struct_size >= sizeof(t5_usb_cdc_class_api_v1) &&
          api->probe && api->line_coding && api->control_lines) {
        driver_ = candidate;
        api_ = api;
        // This class provider contains pure descriptor/protocol logic and
        // needs no GPS-style serial/power kernel primitives.
        if (driver_->start(nullptr)) {
          state_ = State::Active;
          return true;
        }
      }
    }
  }
  (void)unload();
  state_ = State::Failed;
  return false;
}

bool UsbCdcDriverModule::unload() {
  if (driver_) driver_->stop();
  api_ = nullptr;
  driver_ = nullptr;
  if (handle_) {
    if (dlclose(handle_) != 0) {
      state_ = State::Failed;
      return false;
    }
    handle_ = nullptr;
  }
  state_ = State::Absent;
  return true;
}

bool UsbCdcDriverModule::probe(const uint8_t* config, size_t length,
                                uint16_t vid, uint16_t pid,
                                t5_usb_cdc_binding_v1* out) const {
  return state_ == State::Active && api_ && api_->probe(config, length, vid, pid, out);
}

bool UsbCdcDriverModule::lineCoding(uint32_t baud, uint8_t bits, uint8_t parity,
                                     uint8_t stop, uint8_t payload[7]) const {
  return state_ == State::Active && api_ && api_->line_coding(baud, bits, parity, stop, payload);
}

bool UsbCdcDriverModule::controlLines(bool dtr, bool rts, uint16_t* out) const {
  if (state_ != State::Active || !api_ || !out) return false;
  *out = api_->control_lines(dtr, rts);
  return true;
}
