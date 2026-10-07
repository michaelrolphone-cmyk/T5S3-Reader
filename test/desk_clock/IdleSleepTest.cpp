#include "runtime/power/IdleSleepDeadline.h"
#include <cassert>
#include <cstdio>
int main() {
  IdleSleepDeadline idle;
  assert(!idle.observe(100,120000,false,false));
  assert(!idle.observe(120099,120000,false,false));
  assert(idle.observe(120100,120000,false,false));
  idle.activity(120101); // Native-app cleanup/return must not erase pending sleep.
  assert(idle.pending() && idle.observe(120102,120000,true,true));
  assert(idle.consume(120110) && !idle.pending() && !idle.consume(120111));
  assert(!idle.observe(150000,120000,true,false));
  assert(!idle.observe(270000,120000,false,true)); // Existing download/debug/USB policy.
  assert(!idle.observe(389999,120000,false,false));
  assert(idle.observe(390000,120000,false,false));
  idle.consume(0xfffffff0u);
  assert(!idle.observe(static_cast<uint32_t>(0xfffffff0u+119999u),120000,false,false));
  assert(idle.observe(static_cast<uint32_t>(0xfffffff0u+120000u),120000,false,false));
  std::puts("Shared main/native-app idle deadline: exact expiry, inhibition, unwind and rollover PASS");
}
