#include <BoardPowerPort.h>

#if defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
#include <HalStorage.h>

namespace BoardPowerPort {
bool readyForActivation() {
  // Early board/USB wake probes must never attempt to map an ELF before the
  // verified package store is mounted; they may still use the separate gauge.
  return Storage.ready();
}
}  // namespace BoardPowerPort
#endif
