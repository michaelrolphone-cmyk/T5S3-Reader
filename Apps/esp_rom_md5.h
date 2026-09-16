#pragma once

#include "T5AppApi.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ESP_ROM_MD5_SERVICE_INTERVAL (32u * 1024u)

typedef struct {
    uint32_t h[4];
    uint32_t bytes;
    uint32_t used;
    uint32_t next_service;
    uint8_t block[64];
} esp_rom_md5_ctx_t;

static void esp_rom_md5_service_runtime(void) {
    const t5_app_api_v1 *runtime = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!runtime || !runtime->poll) return;
    t5_app_input_t input;
    (void)runtime->poll(&input, 1u);
}

static uint32_t esp_rom_md5_rotl(uint32_t value, uint32_t bits) {
    return (value << bits) | (value >> (32u - bits));
}

static void esp_rom_md5_transform(esp_rom_md5_ctx_t *ctx, const uint8_t block[64]) {
    static const uint8_t shift[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21,
    };
    static const uint32_t k[64] = {
        0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
        0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
        0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
        0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
        0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
        0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
        0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
        0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u,
    };
    uint32_t m[16];
    for (uint32_t i = 0; i < 16u; ++i) {
        const uint8_t *p = block + i * 4u;
        m[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
               ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
    }

    uint32_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    for (uint32_t i = 0; i < 64u; ++i) {
        uint32_t f;
        uint32_t g;
        if (i < 16u) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32u) {
            f = (d & b) | ((~d) & c);
            g = (5u * i + 1u) & 15u;
        } else if (i < 48u) {
            f = b ^ c ^ d;
            g = (3u * i + 5u) & 15u;
        } else {
            f = c ^ (b | (~d));
            g = (7u * i) & 15u;
        }
        const uint32_t old_d = d;
        d = c;
        c = b;
        b = b + esp_rom_md5_rotl(a + f + k[i] + m[g], shift[i]);
        a = old_d;
    }
    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
}

static void esp_rom_md5_init(esp_rom_md5_ctx_t *ctx) {
    ctx->h[0] = 0x67452301u;
    ctx->h[1] = 0xefcdab89u;
    ctx->h[2] = 0x98badcfeu;
    ctx->h[3] = 0x10325476u;
    ctx->bytes = 0;
    ctx->used = 0;
    ctx->next_service = ESP_ROM_MD5_SERVICE_INTERVAL;
}

static void esp_rom_md5_update(esp_rom_md5_ctx_t *ctx, const uint8_t *data, size_t length) {
    ctx->bytes += (uint32_t)length;
    while (length != 0) {
        size_t take = 64u - ctx->used;
        if (take > length) take = length;
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += (uint32_t)take;
        data += take;
        length -= take;
        if (ctx->used == 64u) {
            esp_rom_md5_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }

    if (ctx->bytes >= ctx->next_service) {
        esp_rom_md5_service_runtime();
        do {
            ctx->next_service += ESP_ROM_MD5_SERVICE_INTERVAL;
        } while (ctx->bytes >= ctx->next_service);
    }
}

static void esp_rom_md5_final(esp_rom_md5_ctx_t *ctx, uint8_t digest[16]) {
    const uint32_t original_bytes = ctx->bytes;
    ctx->block[ctx->used++] = 0x80u;
    if (ctx->used > 56u) {
        while (ctx->used < 64u) ctx->block[ctx->used++] = 0;
        esp_rom_md5_transform(ctx, ctx->block);
        ctx->used = 0;
    }
    while (ctx->used < 56u) ctx->block[ctx->used++] = 0;

    const uint32_t bits_low = original_bytes << 3u;
    const uint32_t bits_high = original_bytes >> 29u;
    ctx->block[56] = (uint8_t)bits_low;
    ctx->block[57] = (uint8_t)(bits_low >> 8u);
    ctx->block[58] = (uint8_t)(bits_low >> 16u);
    ctx->block[59] = (uint8_t)(bits_low >> 24u);
    ctx->block[60] = (uint8_t)bits_high;
    ctx->block[61] = (uint8_t)(bits_high >> 8u);
    ctx->block[62] = (uint8_t)(bits_high >> 16u);
    ctx->block[63] = (uint8_t)(bits_high >> 24u);
    esp_rom_md5_transform(ctx, ctx->block);

    for (uint32_t i = 0; i < 4u; ++i) {
        const uint32_t value = ctx->h[i];
        digest[i * 4u + 0u] = (uint8_t)value;
        digest[i * 4u + 1u] = (uint8_t)(value >> 8u);
        digest[i * 4u + 2u] = (uint8_t)(value >> 16u);
        digest[i * 4u + 3u] = (uint8_t)(value >> 24u);
    }
}

static void esp_rom_md5_hex(const uint8_t digest[16], char out[33]) {
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 16u; ++i) {
        out[i * 2u] = hex[digest[i] >> 4u];
        out[i * 2u + 1u] = hex[digest[i] & 0x0fu];
    }
    out[32] = 0;
}
