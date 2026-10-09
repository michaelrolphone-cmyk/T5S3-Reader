#pragma once
#include "Transport.h"
#define RISC_MSC_PUMP_MAX_PASSES 32u
#define RISC_MSC_PUMP_MAX_MS 2u
/* All callbacks are local synchronous functions; none is retained. Each pass
 * performs one bounded hardware service and at most one queued stack event.
 * Check time before every pass and return immediately when idle. One already
 * entered synchronous SD operation retains its own storage timeout; it cannot
 * be preempted by the pump's admission deadline. The caller yields on return. */
static inline uint32_t risc_msc_owner_pump(bool (*ready)(void),void (*hardware)(void),void (*event)(void)) {
 const uint64_t start=risc_msc_now();uint32_t passes=0;
 while(passes<RISC_MSC_PUMP_MAX_PASSES && risc_msc_transport_ok()) {
  if(!ready())break;
  const uint64_t now=risc_msc_now();
  if(now<start || now-start>=RISC_MSC_PUMP_MAX_MS)break;
  hardware();
  if(risc_msc_transport_ok())event();
  ++passes;
 }
 risc_msc_pump_report(passes);return passes;
}
