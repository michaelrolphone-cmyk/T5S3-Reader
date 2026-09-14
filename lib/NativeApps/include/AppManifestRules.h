#pragma once
#include <stdint.h>
#include <string.h>

// Manifests use a stable numeric firmware floor (major.minor.patch). Build
// suffixes are accepted only on the running firmware, never on the floor.
static inline bool t5_parse_version(const char* p, uint32_t out[3], bool build) {
  if (!p) return false;
  for (int i = 0; i < 3; ++i) {
    if (*p < '0' || *p > '9') return false;
    uint32_t n = 0;
    do { if (n > 6553 || (n == 6553 && *p > '5')) return false;
      n = n * 10 + (*p++ - '0');
    } while (*p >= '0' && *p <= '9');
    out[i] = n;
    if (i < 2 && *p++ != '.') return false;
  }
  return !*p || (build && (*p == '-' || *p == '+') && p[1]);
}
static inline bool t5_firmware_compatible(const char* running, const char* minimum) {
  uint32_t a[3], b[3];
  if (!t5_parse_version(running, a, true) || !t5_parse_version(minimum, b, false)) return false;
  for (int i = 0; i < 3; ++i) if (a[i] != b[i]) return a[i] > b[i];
  return true;
}
static inline bool t5_safe_elf_name(const char* name) {
  if (!name) return false;
  const size_t n = strlen(name);
  if (n < 5 || n >= 128 || strcmp(name + n - 4, ".elf") || strstr(name, "..")) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
  }
  return true;
}
// Font Awesome Classic style and Unicode scalar, e.g. solid:f013.
static inline bool t5_parse_icon(const char* icon, bool* regular, uint32_t* cp) {
  if (!icon || !regular || !cp) return false;
  if (!strncmp(icon, "solid:", 6)) { *regular = false; icon += 6; }
  else if (!strncmp(icon, "regular:", 8)) { *regular = true; icon += 8; }
  else return false;
  const size_t n = strlen(icon);
  if (n < 4 || n > 6) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < n; ++i) {
    char c = icon[i];
    unsigned d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                 c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
    if (d == 16) return false;
    v = v * 16 + d;
  }
  if (!v || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return false;
  *cp = v;
  return true;
}
