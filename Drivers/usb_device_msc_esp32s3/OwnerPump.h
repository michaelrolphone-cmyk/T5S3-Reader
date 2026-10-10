#pragma once
#include "Transport.h"
#define RISC_MSC_PUMP_MAX_PASSES 32u
#define RISC_MSC_PUMP_MAX_MS 2u
#define RISC_MSC_PUMP_MAX_PROBES 8192u
/* All callbacks are local synchronous functions; none is retained. Each pass
 * performs one bounded hardware service and at most one queued stack event.
 * Keep an active BOT command moving through brief inter-packet gaps. A
 * full-speed token arrives after the FIFO service, not during it; returning at
 * every gap turns each 64-byte packet into a scheduler sleep. Inactive idle
 * still returns immediately. Bound no-progress probes as well as wall time,
 * including when a broken clock does not advance. One already
 * entered synchronous SD operation retains its own storage timeout; it cannot
 * be preempted by the pump's admission deadline. The caller yields on return. */
static inline uint32_t risc_msc_owner_pump(bool (*ready)(void),void (*hardware)(void),void (*event)(void)) {
 const uint64_t start=risc_msc_now();uint32_t passes=0,probes=0;
 while(passes<RISC_MSC_PUMP_MAX_PASSES && probes++<RISC_MSC_PUMP_MAX_PROBES && risc_msc_transport_ok()) {
  const uint64_t now=risc_msc_now();
  if(now<start || now-start>=RISC_MSC_PUMP_MAX_MS)break;
  if(!ready()) {
   if(!risc_msc_command_pending())break;
   continue;
  }
  hardware();
  if(risc_msc_transport_ok())event();
  ++passes;
 }
 risc_msc_pump_report(passes);return passes;
}
