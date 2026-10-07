#pragma once
#include "InstalledProof.h"
#include "StreamZipProbe.h"
namespace RuntimeBoot {
// One planned qualification batch. Uses the normal boot's existing leases;
// never creates a test graph, installs files, or substitutes capability data.
// Compiled only into the explicitly named qualification environment.
template<class Lifecycle, class Adapter>
void qualificationTick(Lifecycle& lifecycle, Adapter& adapter) {
  static unsigned cycle=0;
  static bool stopped=false, done=false;
  static uint32_t runningSince=0;
  if(done) return;
  if(stopped) {
    if(lifecycle.state()==State::Cold) {
      stopped=false; runningSince=0; lifecycle.start();
      LOG_INF("QUAL", "restart=1");
    } else if(lifecycle.state()==State::Retained) {
      done=true; LOG_ERR("QUAL", "result=failed reason=retained-teardown");
    }
    return;
  }
  if(lifecycle.state()==State::Idle || lifecycle.state()==State::Retained) {
    done=true; LOG_ERR("QUAL", "result=failed reason=boot"); return;
  }
  if(lifecycle.state()!=State::Running) return;
  if(!runningSince) { runningSince=millis(); return; }
  if(millis()-runningSince<5000) return;
  const auto* clock=static_cast<const risc_platform_clock_api_v1*>(adapter.leases[0].interface);
  const auto* zip=static_cast<const risc_archive_zip_api_v1*>(adapter.leases[1].interface);
  bool valid=clock && clock->api_version==1 && clock->struct_size>=sizeof(*clock) && clock->monotonic_ms && clock->sleep_ms &&
    zip && zip->api_version==1 && zip->struct_size>=sizeof(*zip) && zip->begin && zip->append && zip->seal && zip->count && zip->entry && zip->read && zip->close;
  if(valid) {
    auto before=clock->monotonic_ms(clock->context);
    clock->sleep_ms(clock->context,20);
    auto elapsed=clock->monotonic_ms(clock->context)-before;
    valid=elapsed>0 && elapsed<1000;
    LOG_INF("QUAL", "clock_elapsed_ms=%u", unsigned(elapsed));
  }
  int result=valid && verifyInstalledProof() ? streamZipProbe(zip) : 100;
  LOG_INF("QUAL", "cycle=%u result=%d", ++cycle, result);
  if(result) { done=true; LOG_ERR("QUAL", "result=failed reason=consumer"); return; }
  if(cycle==1) { stopped=true; lifecycle.stop(); }
  else { done=true; LOG_INF("QUAL", "result=pass cycles=2 files=8 steady=Running"); }
}
} // namespace RuntimeBoot
