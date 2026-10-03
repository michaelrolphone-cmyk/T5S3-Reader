#include "SdBootReader.h"
#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)
#include "runtime/drivers/BootstrapModuleStore.h"
#include <Board.h>
#include <Logging.h>
bool loadPlatformSdPackages() {
  static bool attempted=false, ready=false;
  if(attempted) return ready;
  attempted=true;
  if(!SdBootReader::mount()) {
    LOG_ERR("SDBOOT","Read-only SD bootstrap mount failed; no fallback");
    return false;
  }
  const bool admitted=RuntimeInstalledProviders::loadBootstrapPackages(
      "/sdboot",Board::id(),SdBootReader::read);
  // Always attempt checked release, even for corrupt/missing packages. No
  // provider start, bus setup or full filesystem owner precedes this barrier.
  const bool released=SdBootReader::release();
  if(!admitted || !released) {
    LOG_ERR("SDBOOT","Handoff refused admitted=%d released=%d",admitted,released);
    return false;
  }
  ready=RuntimeInstalledProviders::finishBootstrapHandoff();
  LOG_INF("SDBOOT","SD package handoff ready=%d",ready);
  return ready;
}
#else
bool loadPlatformSdPackages() { return false; }
#endif
