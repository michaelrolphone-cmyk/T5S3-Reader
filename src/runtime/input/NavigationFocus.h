#pragma once
#include <RiscInputNavigationV1.h>
#include <cstring>

namespace RuntimeInput {
// Copied, token-qualified foreground ownership. Duplicate capability grants
// remain foreground until the last token leaves. No USB names or app pointers.
class NavigationFocus {
    struct Entry { uint32_t token = 0, version = 0; char capability[64]{}; };
    Entry entries_[RISC_INPUT_NAVIGATION_MAX_FOREGROUND]{};
 public:
    bool apply(const risc_input_navigation_api_v1* api) const {
        if (!api) return true;
        risc_input_foreground_v1 claims[RISC_INPUT_NAVIGATION_MAX_FOREGROUND]{};
        size_t count = 0;
        for (const auto& entry : entries_) if (entry.token)
            claims[count++] = {entry.capability, entry.version};
        return api->foreground(api->context, claims, count);
    }
    bool acquire(uint32_t token, const char* capability, uint32_t version,
                 const risc_input_navigation_api_v1* api) {
        if (!token || !capability || !version || std::strlen(capability) >= 64) return false;
        Entry* free = nullptr;
        for (auto& entry : entries_) {
            if (entry.token == token) return false;
            if (!entry.token && !free) free = &entry;
        }
        if (!free) return false;
        free->token = token; free->version = version;
        std::strcpy(free->capability, capability);
        if (apply(api)) return true;
        *free = {};
        (void)apply(api); // Failed handoff never authorizes a foreground consumer.
        return false;
    }
    bool release(uint32_t token, const risc_input_navigation_api_v1* api) {
        for (auto& entry : entries_) if (entry.token == token) {
            entry = {};
            return apply(api);
        }
        return false;
    }
};
}
