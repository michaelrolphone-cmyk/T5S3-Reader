#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_APP_ABI_VERSION 1u

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
} t5_app_api_v1;

const t5_app_api_v1 *t5_app_get_api(uint32_t version);

#ifdef __cplusplus
}
#endif
