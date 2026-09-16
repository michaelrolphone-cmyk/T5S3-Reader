#include "NativeDeviceConsent.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <NativeAppLauncher.h>
#include <T5AppApi.h>
#include <esp_task_wdt.h>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "fontIds.h"
#include "runtime/resources/ExecutionContext.h"

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {
constexpr uint32_t kConsentTimeoutMs = 30000u;

// Never let an arbitrary SD pathname inject controls into trusted UI text.
void displayName(const char* path, char (&out)[32]) {
  std::memset(out, 0, sizeof(out));
  if (!path) return;
  const char* filename = std::strrchr(path, '/');
  filename = filename ? filename + 1 : path;
  for (size_t i = 0; i < sizeof(out) - 1 && filename[i]; ++i) {
    const unsigned char ch = static_cast<unsigned char>(filename[i]);
    out[i] = ch >= 0x20u && ch <= 0x7eu ? static_cast<char>(ch) : '?';
  }
}

void outline(int x, int y, int w, int h) {
  renderer.fillRect(x, y, w, 2, true);
  renderer.fillRect(x, y + h - 2, w, 2, true);
  renderer.fillRect(x, y, 2, h, true);
  renderer.fillRect(x + w - 2, y, 2, h, true);
}
}  // namespace

bool nativeDeviceConsentPrompt(const RuntimeDevices::DeviceInfo& device,
                               const char* capability, uint32_t rights) {
  // runNativeApp already holds the RenderLock and owns the framebuffer. This
  // function does not start an Activity, unload/restart the ELF, or persist a
  // grant across invocation boundaries.
  auto* context = RuntimeResources::ExecutionContext::current();
  const char* path = native_app_current_path();
  if (!context || !context->running(context->id()) ||
      !t5_app_get_api(T5_APP_ABI_VERSION) || !path || !capability ||
      !device.handle || !rights || (rights & ~7u)) return false;
  const uint32_t invocation = context->id();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  if (width < 320 || height < 350) return false;  // No clipped approval control.

  char app[32]{};
  displayName(path, app);
  char appLine[55]{};
  char deviceLine[55]{};
  char capabilityLine[55]{};
  char rightsLine[55]{};
  std::snprintf(appLine, sizeof(appLine), "App: %.30s", app);
  std::snprintf(deviceLine, sizeof(deviceLine), "Device: %.30s", device.label);
  std::snprintf(capabilityLine, sizeof(capabilityLine), "Capability: %.30s", capability);
  std::snprintf(rightsLine, sizeof(rightsLine), "Rights: %s%s%s",
                rights & 1u ? "READ " : "", rights & 2u ? "WRITE " : "",
                rights & 4u ? "CONFIGURE" : "");

  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 20, 34, "RiscRTE DEVICE PERMISSION");
  renderer.drawText(UI_12_FONT_ID, 20, 76, "UNVERIFIED LOCAL APP");
  renderer.drawText(UI_12_FONT_ID, 20, 114, appLine);
  renderer.drawText(UI_12_FONT_ID, 20, 151, deviceLine);
  renderer.drawText(UI_12_FONT_ID, 20, 188, capabilityLine);
  renderer.drawText(UI_12_FONT_ID, 20, 225, rightsLine);
  renderer.drawText(UI_12_FONT_ID, 20, 264, "Allow for this run only?");
  const int buttonY = height - 83;
  const int buttonWidth = width / 2 - 30;
  outline(20, buttonY, buttonWidth, 55);
  outline(width / 2 + 10, buttonY, buttonWidth, 55);
  renderer.drawText(UI_12_FONT_ID, 30, buttonY + 17, "BACK: DENY");
  renderer.drawText(UI_12_FONT_ID, width / 2 + 20, buttonY + 17, "CONFIRM: ALLOW");
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  // A button held while the ELF requested permission must be released before
  // its next press can approve. System Back/Power/Home always cancel.
  using Button = MappedInputManager::Button;
  mappedInputManager.clearInjectedButtonTap();
  mappedInputManager.update();
  bool armed = false;
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) < kConsentTimeoutMs) {
    esp_task_wdt_reset();
    delay(20);
    mappedInputManager.update();
    if (RuntimeResources::ExecutionContext::current() != context ||
        !context->running(invocation) || !t5_app_get_api(T5_APP_ABI_VERSION)) return false;
    if (mappedInputManager.isPressed(Button::Power) ||
        mappedInputManager.wasTouchHomeButtonPressed() ||
        mappedInputManager.isPressed(Button::Back)) return false;
    MappedInputManager::TouchPoint touch{};
    const bool tapped = mappedInputManager.wasTouchTapped(touch, renderer);
    if (!armed) {
      if (!mappedInputManager.isPressed(Button::Confirm)) armed = true;
      continue;
    }
    if (tapped && touch.y >= buttonY && touch.y < buttonY + 55) {
      if (touch.x >= 20 && touch.x < 20 + buttonWidth) return false;
      if (touch.x >= width / 2 + 10 && touch.x < width / 2 + 10 + buttonWidth)
        return true;
    }
    if (mappedInputManager.isPressed(Button::Confirm)) return true;
  }
  return false;
}
