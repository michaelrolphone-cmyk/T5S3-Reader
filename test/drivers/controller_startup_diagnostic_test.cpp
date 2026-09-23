#include "../../Drivers/usb_controller_esp32s3/StartupDiagnostic.h"
#include "../../Drivers/usb_controller_esp32s3/EnumerationDiagnostic.h"
#include <cassert>
#include <cstring>
#include <climits>
static void capture(EnumerationDiagnostic &diagnostic, const char *format, ...) {
  va_list args;
  va_start(args, format);
  diagnostic.error(format, args);
  va_end(args);
}
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
  EnumerationDiagnostic enumeration;
  assert(enumeration.copy(0, out, sizeof(out)));
  assert(!std::strcmp(out, "PORT OFF; NO ENUM EVENT"));
  assert(enumeration.copy(1u << 12, out, sizeof(out)));
  assert(!std::strcmp(out, "NO ATTACH; NO ENUM EVENT"));
  const uint32_t attached = (1u << 12) | 1u;
  enumeration.stage("GET_SHORT_DEV_DESC");
  assert(enumeration.copy(attached, out, sizeof(out)));
  assert(!std::strcmp(out, "ATTACHED; GET_SHORT_DEV_DESC"));
  capture(enumeration, "Bad transfer status %d: %s", -1, "GET_SHORT_DEV_DESC");
  // Cleanup must retain the specific cause rather than replace it with its
  // generic failure message or later cleanup stage.
  enumeration.stage("CLEANUP_FAILED");
  capture(enumeration, "Stage failed: %s", "CLEANUP_FAILED");
  assert(enumeration.copy(attached, out, sizeof(out)));
  assert(!std::strcmp(out, "ATTACHED; ENUM FAIL: Bad transfer status -1: GET_SHORT_DEV_DESC"));
  assert(enumeration.copy(attached, small, sizeof(small)) && small[1] == 0);
  assert(!enumeration.copy(attached, nullptr, 1) && !enumeration.copy(attached, out, 0));
  enumeration.clear(); // A fresh physical attach gets its own first failure.
  capture(enumeration, "LANGID 0x%x not found", 0x409u);
  assert(enumeration.copy(attached, out, sizeof(out)));
  assert(!std::strcmp(out, "ATTACHED; ENUM FAIL: LANGID 0x409 not found"));
  enumeration.clear();
  capture(enumeration, "%d %u %%", INT_MIN, UINT_MAX);
  assert(enumeration.copy(attached, out, sizeof(out)));
  assert(!std::strcmp(out, "ATTACHED; ENUM FAIL: -2147483648 4294967295 %"));
}
