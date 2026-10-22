#include "runtime/power/SleepOrchestration.h"
#include <cassert>
#include <cstdio>
#include <string>

using SleepOrchestration::Operations;
using SleepOrchestration::ShutdownOperations;
struct Fixture {
  bool reserve = true, cancel = true, drain = true, display = true;
  bool available = true, powerOff = true;
  std::string calls;
};
static Fixture& state(void* context) { assert(context); return *static_cast<Fixture*>(context); }
static const Operations operations{
    nullptr,
    [](void* c) { auto& f = state(c); f.calls += 'D'; return f.display; },
    [](void* c) { state(c).calls += 'R'; }};
static const ShutdownOperations shutdown{
    [](void* c) { auto& f = state(c); f.calls += 'P'; return f.reserve; },
    [](void* c) { auto& f = state(c); f.calls += 'C'; return f.cancel; },
    [](void* c) { auto& f = state(c); f.calls += 'G'; return f.drain; },
    [](void* c) { auto& f = state(c); f.calls += 'A'; return f.available; },
    [](void* c) { auto& f = state(c); f.calls += 'B'; return f.powerOff; },
    [](void* c, bool accepted) { state(c).calls += accepted ? 'Y' : 'N'; },
    [](void* c) { state(c).calls += 'U'; }};
int main() {
  for (unsigned mask = 0; mask < 64; ++mask) {
    Fixture f;
    f.reserve = mask & 1; f.cancel = mask & 2; f.drain = mask & 4;
    f.display = mask & 8; f.available = mask & 16; f.powerOff = mask & 32;
    auto ops = operations; ops.context = &f;
    assert(SleepOrchestration::prepare(ops) == f.display);
    assert(f.calls == (f.display ? "D" : "DR"));
    f.calls.clear();
    std::string expected = "P";
    bool expectedReady = false;
    if (!f.reserve && !f.cancel) expected += "CR";
    else if (!f.reserve && !f.drain) expected += "CGR";
    else {
      if (!f.reserve) expected += "CG";
      expected += "D";
      if (!f.display) expected += "CR";
      else {
        expectedReady = true;
        if (!f.reserve) expected += "U";
        else if (!f.available) expected += "AU";
        else expected += f.powerOff ? "ABY" : "ABN";
      }
    }
    assert(SleepOrchestration::prepare(ops, &shutdown) == expectedReady);
    assert(f.calls == expected);
    // Retry starts cleanly; there is no retained orchestration state.
    f.calls.clear();
    assert(SleepOrchestration::prepare(ops, &shutdown) == expectedReady);
    assert(f.calls == expected);
  }
  Fixture f;
  for (unsigned missing = 0; missing < 9; ++missing) {
    auto ops = operations; ops.context = &f;
    auto off = shutdown;
    switch (missing) {
      case 0: ops.powerDownDisplay = nullptr; break;
      case 1: ops.resumeAfterRefusal = nullptr; break;
      case 2: off.reserve = nullptr; break;
      case 3: off.cancel = nullptr; break;
      case 4: off.drain = nullptr; break;
      case 5: off.available = nullptr; break;
      case 6: off.powerOff = nullptr; break;
      case 7: off.returned = nullptr; break;
      case 8: off.unavailable = nullptr; break;
    }
    assert(!SleepOrchestration::prepare(ops, &off));
    assert(f.calls.empty());
  }
  puts("Shared production sleep preparation: 64 ordinary/shutdown/retry matrices and 9 incomplete-operation refusals PASS");
}
