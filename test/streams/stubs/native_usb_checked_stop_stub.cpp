#include "native/NativeUsbClassBridge.h"

// Host-only fault injection for tests linking the actual serial bridge.
// NEVER link into firmware or the production class ELF binding test.
namespace {
bool failCheckedStop = false;
}
void nativeUsbTestFailCheckedStop(bool fail) { failCheckedStop = fail; }
bool nativeUsbClassStopChecked() {
  if (failCheckedStop) return false; // Physical quiescence is uncertain.
  nativeUsbClassStop();
  return true;
}
