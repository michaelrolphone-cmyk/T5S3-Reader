#include <HalDisplay.h>
#include <HalGPIO.h>
#include <T5HardwareTakeover.h>
#include <esp_err.h>
#include <esp_log.h>
#include <stdint.h>

namespace {
constexpr char kTag[] = "ELF_TAKEOVER";
bool s_display_borrowed = false;
bool s_touch_borrowed = false;
}

// These hooks belong to the firmware loader; they are not imported by ELFs.
// The caller holds the native-app RenderLock for the entire launch/session.
extern "C" esp_err_t native_hardware_takeover_begin(uint32_t requested) {
  if (requested == 0U) return ESP_OK;
  if ((requested & ~T5_HARDWARE_TAKEOVER_SUPPORTED) != 0U) {
    ESP_LOGE(kTag, "Unsupported hardware takeover mask 0x%08lx",
             static_cast<unsigned long>(requested));
    return ESP_ERR_NOT_SUPPORTED;
  }
  if (s_display_borrowed) return ESP_ERR_INVALID_STATE;
  if ((requested & T5_HARDWARE_TAKEOVER_DISPLAY) != 0U) {
    // Legacy takeover apps such as GameBoy still access GT911 directly. Stop
    // the firmware worker before they enter so there is never more than one
    // reader acknowledging the controller's READY/release reports. A board
    // with no active firmware touch capture can still hand off its display.
    s_touch_borrowed = gpio.isTouchCaptureRunning();
    if (s_touch_borrowed && !gpio.suspendTouchCapture()) {
      s_touch_borrowed = false;
      ESP_LOGE(kTag, "Touch input could not quiesce; refusing ELF entry");
      return ESP_ERR_INVALID_STATE;
    }
    if (!display.suspendForExternalOwner()) {
      s_touch_borrowed = false;
      (void)gpio.resumeTouchCapture();
      ESP_LOGE(kTag, "Display could not be relinquished; refusing ELF entry");
      return ESP_ERR_INVALID_STATE;
    }
    s_display_borrowed = true;
    ESP_LOGI(kTag, "Exclusive display/touch hardware ownership transferred to ELF");
  }
  return ESP_OK;
}

extern "C" esp_err_t native_hardware_takeover_end(uint32_t requested) {
  if (requested == 0U) return ESP_OK;
  if ((requested & T5_HARDWARE_TAKEOVER_DISPLAY) != 0U) {
    if (!s_display_borrowed) return ESP_ERR_INVALID_STATE;
    // app_main must already have terminated its scan task, DMA and callbacks.
    // A return without relinquishing app-owned hardware is an app bug.
    s_display_borrowed = false;
    const bool displayRestored = display.resumeFromExternalOwner();
    if (!displayRestored)
      ESP_LOGE(kTag, "Failed to reinitialize the firmware display after ELF exit");

    // Display resume may recreate the shared board bus, so GT911 is restored
    // only after display ownership is back in firmware.
    const bool touchRestored = !s_touch_borrowed || gpio.resumeTouchCapture();
    if (touchRestored) s_touch_borrowed = false;
    else ESP_LOGE(kTag, "Failed to restore firmware touch capture after ELF exit");

    if (!displayRestored || !touchRestored) return ESP_FAIL;
    display.requestNextRefresh(HalDisplay::FULL_REFRESH);
    ESP_LOGI(kTag, "Exclusive display/touch ownership restored to RiscRTE");
  }
  return ESP_OK;
}
