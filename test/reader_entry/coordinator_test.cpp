#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "activities/ActivityManager.h"
#include "native/NativeReaderEntry.h"
#include "T5ReaderEntryApi.h"
#include "esp_err.h"

class GfxRenderer {};
class MappedInputManager {};
ActivityManager activityManager;
extern "C" void app_main();
using NativeReaderEntry::Action;
static Activity activity;
static const char* activePath = nullptr;
static void* task = reinterpret_cast<void*>(1);
static int loads, unloads, starts, loops, children, sleeps, delays, watchdogs;
static bool loaderMapped, resumeOkay = true;
static std::string scenario;
static std::vector<std::string> events;

void* xTaskGetCurrentTaskHandle() { return task; }
void delay(unsigned long ms) { assert(ms > 0); ++delays; }
int esp_task_wdt_reset() { ++watchdogs; return 0; }
extern "C" bool native_app_loader_retained() { return scenario == "retained" && unloads != 0; }
extern "C" const char* native_app_current_path() { return activePath; }
esp_err_t runNativeReaderEntry(const char* path, GfxRenderer&, MappedInputManager&) {
  assert(NativeReaderEntry::mapped());
  assert(!loaderMapped && !activePath);
  ++loads;
  events.emplace_back("load");
  if (scenario == "load_error") return ESP_FAIL;
  loaderMapped = true;
  activePath = path;
  assert(t5_reader_entry_get_api(T5_READER_ENTRY_ABI_VERSION));
  assert(!t5_reader_entry_get_api(99));
  task = reinterpret_cast<void*>(2);
  assert(!t5_reader_entry_get_api(T5_READER_ENTRY_ABI_VERSION));
  assert(!NativeReaderEntry::deferActivity(&activity));
  task = reinterpret_cast<void*>(1);
  app_main(); // Real distributable entry source, not an emulated app loop.
  events.emplace_back("unload");
  ++unloads;
  activePath = nullptr;
  loaderMapped = false;
  return scenario == "unload_error" || scenario == "retained" ? ESP_FAIL : ESP_OK;
}
bool ActivityManager::resumeNativeAppLoop(Activity* target) {
  assert(target == &activity && !loaderMapped && !NativeReaderEntry::mapped());
  assert(activePath == nullptr);
  events.emplace_back("child");
  // A production activity can synchronously launch a child and then its parent
  // within this one resumed loop; neither call may reload default.elf midway.
  ++children;
  assert(loads == unloads);
  events.emplace_back("child-resume");
  return resumeOkay;
}
static void start() {
  ++starts;
  assert(loaderMapped && NativeReaderEntry::mapped());
  if (scenario == "startup_handoff")
    assert(NativeReaderEntry::deferActivity(&activity));
}
static void loop() {
  ++loops;
  assert(loops < 20); // A broken continuation must not spin indefinitely.
  assert(!NativeReaderEntry::pending());
  assert(t5_reader_entry_get_api(T5_READER_ENTRY_ABI_VERSION)->pump() == T5_READER_ENTRY_STOP);
  if (scenario == "startup_handoff") assert(false && "loop ran after startup handoff");
  if (scenario == "normal" && loads == 1 && loops == 1) return;
  if (loads == 1) {
    assert(NativeReaderEntry::deferActivity(&activity));
    assert(NativeReaderEntry::deferActivity(&activity));
    return;
  }
  if (scenario == "normal" && loads == 2) {
    assert(NativeReaderEntry::deferSleep(Action::SleepKeepingScreen, false));
    return;
  }
  // Tell the real callback to STOP on the next pump; the loader is the only
  // boundary that supplies/withdraws this path in production.
  activePath = nullptr;
}
static void sleep(Action action, bool wakeOnTouch) {
  assert(!loaderMapped && !NativeReaderEntry::mapped());
  assert(action == Action::SleepKeepingScreen && !wakeOnTouch);
  ++sleeps;
  events.emplace_back("sleep-return");
}
int main(int argc, char** argv) {
  assert(argc == 2);
  scenario = argv[1];
  if (scenario == "resume_error" || scenario == "startup_handoff") resumeOkay = false;
  GfxRenderer renderer;
  MappedInputManager input;
  assert(!t5_reader_entry_get_api(T5_READER_ENTRY_ABI_VERSION));
  assert(!NativeReaderEntry::deferActivity(&activity));
  assert(!NativeReaderEntry::run(nullptr, renderer, input, start, loop, sleep));
  const bool result = NativeReaderEntry::run("/sd/Apps/paperspace/default.elf", renderer, input,
                                            start, loop, sleep);
  assert(!NativeReaderEntry::mapped() && !NativeReaderEntry::pending());
  assert(!t5_reader_entry_get_api(T5_READER_ENTRY_ABI_VERSION));
  if (scenario == "normal") {
    assert(result && loads == 3 && unloads == 3 && children == 1 && sleeps == 1);
    assert((events == std::vector<std::string>{"load", "unload", "child", "child-resume",
                "load", "unload", "sleep-return", "load", "unload"}));
    assert(NativeReaderEntry::consumeResume());
    assert(!NativeReaderEntry::consumeResume());
    assert(delays > 0 && watchdogs > 0);
  } else if (scenario == "load_error" || scenario == "unload_error" || scenario == "retained") {
    assert(!result && loads == 1 && children == 0 && sleeps == 0);
    assert(NativeReaderEntry::blocked() == (scenario == "retained"));
    assert(!NativeReaderEntry::run("/sd/Apps/paperspace/default.elf", renderer, input,
                                    start, loop, sleep));
    assert(loads == 1); // Failed teardown may neither dispatch nor retry a map.
  } else if (scenario == "resume_error" || scenario == "startup_handoff") {
    assert(!result && loads == 1 && unloads == 1 && children == 1 && sleeps == 0);
    if (scenario == "startup_handoff") assert(loops == 0);
  } else {
    assert(false && "unknown test case");
  }
  std::printf("Actual Reader coordinator %s PASS\n", scenario.c_str());
}
