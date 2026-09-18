#pragma once

#include <stdbool.h>
#include <stdint.h>

// Numeric major.minor.patch, each component in uint32_t. Matches the
// ordinary package preflight comparison policy (no suffix or truncation).
// Result: -1 if candidate older, 0 equal, 1 newer, 2 malformed.
static inline bool t5_package_version_parts(const char *text, uint32_t parts[3]) {
    if (!text || !parts) return false;
    const char *p = text;
    for (unsigned i = 0; i < 3; ++i) {
        if (*p < '0' || *p > '9') return false;
        uint32_t n = 0;
        do {
            const uint32_t digit = (uint32_t)(*p - '0');
            if (n > (UINT32_MAX - digit) / 10u) return false;
            n = n * 10u + digit;
            ++p;
        } while (*p >= '0' && *p <= '9');
        parts[i] = n;
        if (i < 2) {
            if (*p != '.') return false;
            ++p;
        } else if (*p != '\0') {
            return false;
        }
    }
    return true;
}

static inline int t5_package_version_compare(const char *candidate,
                                             const char *installed) {
    uint32_t a[3] = {0}, b[3] = {0};
    if (!t5_package_version_parts(candidate, a) ||
        !t5_package_version_parts(installed, b)) return 2;
    for (unsigned i = 0; i < 3; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}
