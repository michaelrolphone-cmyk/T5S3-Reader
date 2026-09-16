#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
struct md5_context_t { uint32_t count = 0; };
inline void esp_rom_md5_init(md5_context_t* ctx) { ctx->count = 0; }
inline void esp_rom_md5_update(md5_context_t* ctx, const void*, uint32_t size) { ctx->count += size; }
// Deterministic test digest. This harness verifies provider control flow, not
// the ESP-IDF MD5 implementation, which is exercised by the firmware builds.
inline void esp_rom_md5_final(uint8_t* out, md5_context_t*) { std::memset(out, 0, 16); }
