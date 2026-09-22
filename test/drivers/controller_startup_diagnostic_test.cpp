#include "../../Drivers/usb_controller_esp32s3/StartupDiagnostic.h"
#include <cassert>
#include <cstring>
#include <climits>
int main() {
  StartupDiagnostic diagnostic;
  char out[112];
  assert(!diagnostic.copy(out, sizeof(out)) && !out[0]);
  diagnostic.failure("usb-host-install", 259);
  assert(diagnostic.copy(out, sizeof(out)));
  assert(!std::strcmp(out, "usb-host-install rc=259 (0x103)"));
  diagnostic.failure("transfer-alloc", INT32_MIN);
  assert(diagnostic.copy(out, sizeof(out)));
  assert(!std::strcmp(out, "transfer-alloc rc=-2147483648 (0x80000000)"));
  char small[2] = {'?', '?'};
  assert(diagnostic.copy(small, sizeof(small)) && small[1] == 0);
  assert(!diagnostic.copy(nullptr, 1) && !diagnostic.copy(out, 0));
  diagnostic.clear();
  for (unsigned i = 0; i < 200; ++i) diagnostic.text("0123456789");
  assert(diagnostic.copy(out, sizeof(out)) && std::strlen(out) == 111);
}
