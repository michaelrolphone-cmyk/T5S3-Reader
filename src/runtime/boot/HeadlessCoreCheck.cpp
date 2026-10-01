// Deliberately excluded from normal reader builds. This checkpoint runs the
// existing runtime primitives on hardware; it is not a package loader, camera
// driver, second stream implementation or replacement production entry point.
#ifdef RISCRTE_HEADLESS_CORE_CHECK
#include <Arduino.h>
#include <esp_system.h>
#include <cstring>

#include "../capabilities/DeviceRegistry.h"
#include "../resources/ExecutionContext.h"
#include "../streams/StreamRuntime.h"

// This development image has no persistent settings or NVS consumers. Arduino
// startup otherwise may erase a newer-format NVS partition during downgrade.
extern "C" esp_err_t __wrap_nvs_flash_init() { return ESP_OK; }

namespace {
RuntimeStreams::Registry streams;
RuntimeResources::ExecutionContext context;
bool corePassed = false;
uint32_t ticks = 0;
constexpr uint32_t kCheckDeadlineMs = 250;
constexpr unsigned kMaxPumpSteps = 8;

void releaseStreams(void* opaque, uint32_t owner) {
  static_cast<RuntimeStreams::Registry*>(opaque)->release(owner);
}

// Owner-task-only, finite memory and work. Checks real queue routing and
// generation/lifetime behavior without registering a fabricated physical device.
bool checkCore() {
  using RuntimeResources::ExecutionContext;
  if (!context.begin()) return false;
  const uint32_t owner = context.id();
  if (!context.track(ExecutionContext::Resource::Streams, releaseStreams, &streams)) {
    context.end();
    return false;
  }
  t5_stream_t source = 0, destination = 0;
  t5_pipe_t pipe = 0;
  constexpr char payload[] = "riscrte.core.boot.v1";
  char received[sizeof(payload)]{};
  uint32_t transferred = 0;
  bool ok = RuntimeDevices::systemRegistry().count() == 0 &&
      streams.buffer(owner, 64, &source) == T5_STREAM_OK &&
      streams.buffer(owner, 64, &destination) == T5_STREAM_OK &&
      streams.write(owner, source, payload, sizeof(payload), &transferred) == T5_STREAM_OK &&
      transferred == sizeof(payload) &&
      streams.finish(owner, source) == T5_STREAM_OK &&
      streams.connect(owner, source, destination, T5_PIPE_BLOCK_PRODUCER, &pipe) == T5_STREAM_OK;
  const uint32_t started = millis();
  for (unsigned step = 0; ok && streams.runnable() && step < kMaxPumpSteps; ++step) {
    if (millis() - started >= kCheckDeadlineMs) { ok = false; break; }
    streams.pump();  // existing bounded scheduler: one chunk per pipe
    delay(1);        // actual FreeRTOS cooperation, even at low throughput
  }
  t5_pipe_info_t info{};
  info.struct_size = sizeof(info);
  ok = ok && streams.pipeInfo(owner, pipe, &info) == T5_STREAM_OK &&
       info.state == T5_PIPE_DONE && info.bytes_transferred == sizeof(payload) &&
       streams.read(owner, destination, received, sizeof(received), &transferred) == T5_STREAM_OK &&
       transferred == sizeof(payload) && std::memcmp(payload, received, sizeof(payload)) == 0;
  context.end();  // existing cleanup owns release; no dangling queue handles
  t5_stream_info_t stale{};
  stale.struct_size = sizeof(stale);
  return ok && context.state() == ExecutionContext::State::Terminated &&
      ExecutionContext::current() == nullptr && !streams.runnable() &&
      streams.info(owner, source, &stale) == T5_STREAM_INVALID &&
      streams.info(owner, destination, &stale) == T5_STREAM_INVALID;
}
}  // namespace

void setup() {
  Serial.begin(115200);  // reserved one-way port diagnostics; no UART capability
  delay(300);
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  Serial.printf("RTE_CORE_BOOT revision=%s flash=%u psram=%u reset=%u\n",
      RISCRTE_CORE_CHECK_REVISION, ESP.getFlashChipSize(), ESP.getPsramSize(),
      static_cast<unsigned>(esp_reset_reason()));
  Serial.printf("RTE_CORE_MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  corePassed = checkCore();
  Serial.printf("RTE_CORE_CHECK streams_context_cleanup=%s devices=%u\n",
      corePassed ? "PASS" : "FAIL",
      static_cast<unsigned>(RuntimeDevices::systemRegistry().count()));
  Serial.println("RTE_CORE_LIMIT packages=unavailable provisioning=unimplemented storage=unmounted ui=absent");
}

void loop() {
  if (corePassed) streams.pump();  // same owner task, no GUI/input dependency
  ++ticks;
  static uint32_t reported = 0;
  const uint32_t now = millis();
  if (now - reported >= 5000) {
    reported = now;
    Serial.printf("RTE_CORE_IDLE state=%s ticks=%lu uptime_ms=%lu heap=%u devices=%u\n",
        corePassed ? "ready" : "fault", static_cast<unsigned long>(ticks),
        static_cast<unsigned long>(now), ESP.getFreeHeap(),
        static_cast<unsigned>(RuntimeDevices::systemRegistry().count()));
  }
  delay(10);  // low-work stable idle; no filesystem scans or retry loops
}
#endif
