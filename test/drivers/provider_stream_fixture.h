#pragma once
#include <RiscProviderV2.h>
typedef struct {
    const risc_stream_provider_v1 *(*streams)(void);
    uint32_t (*source)(void);
    void (*block_quiesce)(bool);
    uint32_t (*polls)(void);
} provider_stream_fixture_api;
