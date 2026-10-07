#include "NativeReaderEntry.h"
#include <RuntimeFaultRetention.h>
#include "NativeAppHost.h"
#include "activities/ActivityManager.h"
#include <NativeAppLauncher.h>
#include <T5ReaderEntryApi.h>
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>

namespace NativeReaderEntry {
namespace {
State state;
TaskHandle_t owner = nullptr;
void (*startCallback)() = nullptr;
void (*loopCallback)() = nullptr;
bool resumed = false;
bool pumping = false;
uint32_t pump() {
  if (!mapped() || pumping || !loopCallback || !native_app_current_path())
    return T5_READER_ENTRY_STOP;
  if (state.pending()) return T5_READER_ENTRY_HANDOFF;
  pumping = true;
  if (startCallback) startCallback();
  if (!state.pending()) loopCallback();
  pumping = false;
  esp_task_wdt_reset();
  // Even fast web-server / screenshot early-return paths must cooperate.
  delay(1);
  return state.pending() ? T5_READER_ENTRY_HANDOFF : T5_READER_ENTRY_CONTINUE;
}
const t5_reader_entry_api_v1 api{T5_READER_ENTRY_ABI_VERSION, sizeof(api), pump};
}  // namespace
bool blocked() { return native_app_loader_retained() || risc_runtime_retention_required(); }
bool mapped() { return state.mapped() && owner == xTaskGetCurrentTaskHandle(); }
bool pending() { return state.pending(); }
bool deferActivity(Activity* activity) {
  return activity && mapped() && pumping && state.request(Action::ActivityLoop, activity);
}
bool deferSleep(Action action, bool wakeOnTouch) {
  return mapped() && pumping && state.request(action, nullptr, wakeOnTouch);
}
bool consumeResume() { const bool value = resumed; resumed = false; return value; }
bool run(const char* path, GfxRenderer& renderer, MappedInputManager& input,
         void (*start)(), void (*loop)(), void (*sleep)(Action, bool)) {
  if (owner || state.failed() || !path || !start || !loop || !sleep) return false;
  owner = xTaskGetCurrentTaskHandle();
  startCallback = start;
  loopCallback = loop;
  bool okay = true;
  while (state.begin()) {
    // This path takes no native drawing session or RenderLock. It performs
    // ordinary package admission and unloads before any pending work is taken.
    const auto result = runNativeReaderEntry(path, renderer, input);
    state.end(result == ESP_OK);
    if (result != ESP_OK) { okay = false; break; }
    const Pending next = state.take();
    if (next.action == Action::None) break;
    if (next.action == Action::ActivityLoop) {
      if (!activityManager.resumeNativeAppLoop(static_cast<Activity*>(next.activity))) {
        okay = false;
        break;
      }
    } else {
      sleep(next.action, next.wakeOnTouch);
    }
    if (blocked()) { okay = false; break; }
    resumed = true;
    esp_task_wdt_reset();
    delay(1);
  }
  startCallback = nullptr;
  loopCallback = nullptr;
  owner = nullptr;
  return okay;
}
}  // namespace NativeReaderEntry
extern "C" const t5_reader_entry_api_v1* t5_reader_entry_get_api(uint32_t version) {
  return version == T5_READER_ENTRY_ABI_VERSION && NativeReaderEntry::mapped()
      ? &NativeReaderEntry::api : nullptr;
}
