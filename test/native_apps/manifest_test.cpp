#include <cassert>
#include "AppManifestRules.h"
int main() {
  assert(t5_firmware_compatible("1.1.5", "1.1.5"));
  assert(t5_firmware_compatible("1.1.5-dev-feature-apps-abcd", "1.1.5"));
  assert(t5_firmware_compatible("1.10.0", "1.9.99"));
  assert(!t5_firmware_compatible("1.1.4", "1.1.5"));
  assert(!t5_firmware_compatible("garbage", "1.1.5"));
  assert(!t5_firmware_compatible("1.1.5", "1.1.5-dev"));
  assert(!t5_firmware_compatible("1.1.5", "1.1"));
  assert(!t5_firmware_compatible("1.1.5", "1.1.65536"));
  assert(t5_safe_elf_name("games__mahjong.elf"));
  assert(!t5_safe_elf_name("../app.elf"));
  assert(!t5_safe_elf_name("/app.elf"));
  assert(!t5_safe_elf_name("app\\x.elf"));
  assert(!t5_safe_elf_name("app.json"));
  bool regular; uint32_t cp;
  assert(t5_parse_icon("solid:f013", &regular, &cp) && !regular && cp == 0xf013);
  assert(t5_parse_icon("regular:F007", &regular, &cp) && regular && cp == 0xf007);
  assert(!t5_parse_icon("solid:110000", &regular, &cp));
  assert(!t5_parse_icon("solid:d800", &regular, &cp));
  assert(!t5_parse_icon("solid:f013junk", &regular, &cp));
  assert(!t5_parse_icon("brand:f013", &regular, &cp));
}
