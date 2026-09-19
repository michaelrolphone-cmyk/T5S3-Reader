#include "native/NativeUsbClassBridge.h"
#include <T5UsbApi.h>
#include <cstddef>

// The stream bridge fixture owns the USB serial fake. This translation unit
// adapts it to the installed-class interface without changing production
// routing. Never link it with the production NativeUsbClassBridge.cpp.
bool nativeUsbClassEnsureInstalled(uint16_t) { return nativeUsbClassAvailable(); }
bool nativeUsbClassAttachPair(uint32_t owner, t5_stream_t rx, t5_stream_t tx) {
  return owner != 0 && rx != 0 && tx != 0 && rx != tx;
}
int32_t nativeUsbClassRead(uint8_t* data, uint32_t capacity, uint32_t* count) {
  if (count) *count = 0;
  if (!data || !capacity || !count) return T5_STREAM_INVALID;
  const auto* api = t5_usb_get_api(T5_USB_API_VERSION);
  if (!api || !api->serial_read) return T5_STREAM_UNSUPPORTED;
  const size_t received = api->serial_read(data, capacity);
  if (received > capacity) return T5_STREAM_IO;
  *count = static_cast<uint32_t>(received);
  return received ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
int32_t nativeUsbClassWrite(const uint8_t* data, uint32_t length, uint32_t* count) {
  if (count) *count = 0;
  if (!data || !length || !count) return T5_STREAM_INVALID;
  const auto* api = t5_usb_get_api(T5_USB_API_VERSION);
  if (!api || !api->serial_write) return T5_STREAM_UNSUPPORTED;
  const size_t written = api->serial_write(data, length);
  if (written > length) return T5_STREAM_IO;
  *count = static_cast<uint32_t>(written);
  return written ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
