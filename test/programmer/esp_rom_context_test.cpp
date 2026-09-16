// Compile the existing mock transport and REAL NativeEspRomBridge provider in
// this second binary; reuse its fixture without running the baseline main.
// The original main omits an explicit return, legal for main but not for the
// renamed fixture function; suppress only that one translation-unit warning.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main espRomBaselineMain
#include "esp_rom_provider_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "runtime/resources/ExecutionContext.h"

using RuntimeResources::ExecutionContext;
namespace {
struct StopScenario {
  uint8_t stage;
  bool end;
  bool fired = false;
  unsigned calls = 0;
  unsigned callsAtStop = 0;
  uint32_t invocation = 0;
};
bool stopAtStage(void* opaque, const t5_program_esp_rom_status_v1* status) {
  auto& test = *static_cast<StopScenario*>(opaque);
  ++test.calls;
  assert(!test.fired && "Progress callback invoked after context stop");
  if (status->stage == test.stage &&
      (test.stage != T5_PROGRAM_STAGE_HASH || status->percent >= 5) &&
      (test.stage != T5_PROGRAM_STAGE_WRITE || status->percent >= 5)) {
    auto* owner = ExecutionContext::current();
    assert(owner && owner->running(test.invocation));
    test.fired = true;
    test.callsAtStop = test.calls;
    if (test.end) owner->end();
    else owner->requestStop();
    // Synchronous programming MUST remain registered until it returns. Even
    // reentrant end() cannot free the input or serial streams in this frame.
    assert(owner->state() == ExecutionContext::State::Stopping);
    assert(ExecutionContext::current() == owner);
    assert(!t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION));
  }
  return true;
}
void stopCase(uint8_t stage, bool end) {
  ExecutionContext owner;
  assert(owner.begin());
  StopScenario test{stage, end};
  test.invocation = owner.id();
  resetFixture(Fault::None);
  auto* api = t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION);
  assert(api && api->program);
  t5_program_esp_rom_status_v1 result{};
  const auto rc = api->program(1, 0x10000u, stopAtStage, &test, &result);
  assert(rc == T5_PROGRAM_CANCELLED && result.result == T5_PROGRAM_CANCELLED);
  assert(test.fired && test.calls == test.callsAtStop);
  assert(!fixture.completeReported && !fixture.leaseLive);
  assert(fixture.acquired == fixture.released);
  if (stage == T5_PROGRAM_STAGE_HASH) assert(fixture.acquired == 0);
  else assert(fixture.acquired == 1);
  if (stage == T5_PROGRAM_STAGE_WRITE) assert(result.bytes_written > 0);
  else assert(result.bytes_written == 0);
  assert(owner.state() == ExecutionContext::State::Terminated);
  assert(!ExecutionContext::current());
  assert(!owner.untrack(ExecutionContext::Resource::Programmer, test.invocation));
  owner.end();
  assert(owner.begin());
  assert(owner.id() != test.invocation);
  assert(!owner.untrack(ExecutionContext::Resource::Programmer, test.invocation));
  owner.end();
}
} // namespace
int main() {
  stopCase(T5_PROGRAM_STAGE_HASH, false);
  stopCase(T5_PROGRAM_STAGE_HASH, true);
  stopCase(T5_PROGRAM_STAGE_ERASE, false);
  stopCase(T5_PROGRAM_STAGE_WRITE, true);
  resetFixture(Fault::None);
  t5_program_esp_rom_status_v1 denied{};
  assert(!t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION));
  // A previously obtained ABI pointer cannot bypass a terminated context.
  ExecutionContext next;
  assert(next.begin());
  auto* api = t5_program_esp_rom_get_api(T5_PROGRAM_ESP_ROM_API_VERSION);
  assert(api);
  next.end();
  assert(api->program(1, 0x10000u, nullptr, nullptr, &denied) == T5_PROGRAM_DENIED);
  assert(denied.result == T5_PROGRAM_DENIED && fixture.acquired == 0);
  assert(next.begin());
  auto successful = run(Fault::None, T5_PROGRAM_OK);
  assert(successful.bytes_written == 0x10000u && next.state() == ExecutionContext::State::Running);
  next.end();
  std::puts("ESP ROM invocation cancellation/teardown fault-injection tests passed");
}
