#include "native/NativeUsbClassBridge.h"

// Host tests that link their own class-ELF fake and NativeSerialPortBridge.cpp
// link this adapter, not the physical installed-provider graph. Production
// builds and the real-class bridge binding test MUST NOT link this file.
bool nativeUsbClassStopChecked() {
  nativeUsbClassStop();
  return true;
}
