#pragma once
#include <RiscProviderV2.h>
#include <RiscPackageResourcesV1.h>
typedef struct {
    const risc_stream_provider_v1 *(*streams)(void);
    uint32_t (*source)(void);
    void (*block_quiesce)(bool);
    uint32_t (*polls)(void);
    void (*automatic)(bool);
    uint32_t (*records)(void);
    uint32_t (*sink)(void);
    uint32_t (*consumed)(void);
    uint32_t (*record_sink)(void);
    uint32_t (*records_consumed)(void);
    int32_t (*read_resource)(char* out);
    int32_t (*read_import)(uint32_t index, const char* name, char* out);
} provider_stream_fixture_api;
