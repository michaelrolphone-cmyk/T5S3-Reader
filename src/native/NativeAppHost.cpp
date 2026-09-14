#include "NativeAppHost.h"
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <NativeAppLauncher.h>
#include <T5AppApi.h>
#include <esp_task_wdt.h>
#include <algorithm>
#include <cstring>
#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "fontIds.h"

namespace {
struct Session {
  GfxRenderer& renderer;
  MappedInputManager& input;
  TaskHandle_t owner;
  HalFile directory;
  bool exiting = false;
};
Session* session = nullptr;
bool returned = false;
Session* current() { return session && session->owner == xTaskGetCurrentTaskHandle() ? session : nullptr; }
int32_t width() { auto* s = current(); return s ? s->renderer.getScreenWidth() : 0; }
int32_t height() { auto* s = current(); return s ? s->renderer.getScreenHeight() : 0; }
void clear() { if (auto* s = current()) s->renderer.clearScreen(); }
void text(int32_t x, int32_t y, const char* value) {
  if (auto* s = current(); s && value) s->renderer.drawText(UI_12_FONT_ID, x, y, value);
}
void rect(int32_t x, int32_t y, int32_t w, int32_t h, bool black) {
  if (auto* s = current(); s && w > 0 && h > 0) s->renderer.fillRect(x, y, w, h, black);
}
void present(bool full) {
  if (auto* s = current()) {
    esp_task_wdt_reset();
    s->renderer.displayBuffer(full ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH);
    esp_task_wdt_reset();
  }
}
bool poll(t5_app_input_t* out, uint32_t waitMs) {
  auto* s = current();
  if (!s || !out) return false;
  esp_task_wdt_reset();
  delay(std::max(1u, std::min(waitMs, 50u)));
  s->input.update();
  *out = {};
  using Button = MappedInputManager::Button;
  const Button buttons[] = {Button::Back, Button::Confirm, Button::Left, Button::Right, Button::Up, Button::Down};
  for (unsigned i = 0; i < 6; ++i) {
    if (s->input.isPressed(buttons[i])) out->buttons |= 1u << i;
  }
  MappedInputManager::TouchPoint point{};
  out->tapped = s->input.wasTouchTapped(point, s->renderer);
  if (out->tapped) { out->touch_x = point.x; out->touch_y = point.y; }
  if (s->input.isPressed(Button::Back) || s->input.isPressed(Button::Power) || s->input.wasTouchHomeButtonPressed()) {
    s->exiting = true;
  }
  out->exit_requested = s->exiting;
  return true;
}
uint32_t clockMs() { return ::millis(); }

const char* storagePath(const char* path) {
  if (!path) return nullptr;
  if (std::strcmp(path, "/sd") == 0) return "/";
  if (std::strncmp(path, "/sd/", 4) == 0) return path + 3;
  return nullptr;
}

bool dirOpen(const char* path) {
  auto* s = current();
  const char* sdPath = storagePath(path);
  if (!s || !sdPath || !Storage.ready()) return false;
  if (s->directory.isOpen()) s->directory.close();
  s->directory = Storage.open(sdPath, O_RDONLY);
  if (!s->directory.isOpen() || !s->directory.isDirectory()) {
    if (s->directory.isOpen()) s->directory.close();
    return false;
  }
  s->directory.rewindDirectory();
  return true;
}

bool dirNext(t5_app_dirent_t* out) {
  auto* s = current();
  if (!s || !out || !s->directory.isOpen() || !s->directory.isDirectory()) return false;
  *out = {};
  HalFile entry = s->directory.openNextFile();
  if (!entry.isOpen()) return false;
  entry.getName(out->name, sizeof(out->name));
  out->name[sizeof(out->name) - 1] = '\0';
  out->is_directory = entry.isDirectory() ? 1u : 0u;
  out->size = out->is_directory ? 0u : entry.fileSize64();
  entry.close();
  return true;
}

void dirClose() {
  if (auto* s = current(); s && s->directory.isOpen()) s->directory.close();
}

const t5_app_api_v1 api = {T5_APP_ABI_VERSION, sizeof(t5_app_api_v1), width, height, clear, text, rect,
                          present, poll, clockMs, dirOpen, dirNext, dirClose};
}  // namespace

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return version == T5_APP_ABI_VERSION && current() ? &api : nullptr;
}

esp_err_t runNativeApp(const char* path, GfxRenderer& renderer, MappedInputManager& input) {
  // Render task must not paint the browser over native app output.
  if (session) return ESP_ERR_INVALID_STATE;
  HalPowerManager::Lock powerLock;
  RenderLock lock;
  const auto orientation = renderer.getOrientation();
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  input.clearInjectedButtonTap();
  input.update();
  Session active{renderer, input, xTaskGetCurrentTaskHandle()};
  session = &active;
  esp_task_wdt_reset();
  const esp_err_t result = launch_elf_app(path);
  if (active.directory.isOpen()) active.directory.close();
  session = nullptr;
  renderer.setOrientation(orientation);
  renderer.setRenderMode(mode);
  renderer.requestNextRefresh(HalDisplay::FULL_REFRESH);
  // Consume the exit gesture before returning control to the browser.
  unsigned long quiet = millis();
  do {
    input.update();
    if (input.wasAnyPressed() || input.wasAnyReleased() || input.wasTouchHomeButtonPressed() ||
        input.isPressed(MappedInputManager::Button::Back) || input.isPressed(MappedInputManager::Button::Power)) {
      quiet = millis();
    }
    esp_task_wdt_reset();
    delay(10);
  } while (millis() - quiet < 350);
  input.clearInjectedButtonTap();
  returned = true;
  return result;
}

bool consumeNativeAppReturn() { const bool value = returned; returned = false; return value; }
