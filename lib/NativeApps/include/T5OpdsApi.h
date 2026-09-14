#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_OPDS_API_VERSION 1u
#define T5_OPDS_MAX_SERVERS 8u
#define T5_OPDS_NAME_MAX 64u
#define T5_OPDS_URL_MAX 128u
#define T5_OPDS_USERNAME_MAX 64u
#define T5_OPDS_PASSWORD_MAX 64u

typedef struct {
    char name[T5_OPDS_NAME_MAX];
    char url[T5_OPDS_URL_MAX];
    char username[T5_OPDS_USERNAME_MAX];
    char password[T5_OPDS_PASSWORD_MAX];
} t5_opds_server_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // OPDS configuration remains firmware-owned. The trusted first-party ELF
    // receives copied records and asks firmware to persist mutations through the
    // existing OpdsServerStore (including its password-at-rest handling).
    uint32_t (*count)(void);
    bool (*read)(uint32_t index, t5_opds_server_t *server);
    bool (*add)(const t5_opds_server_t *server, uint32_t *new_index);
    bool (*update)(uint32_t index, const t5_opds_server_t *server);
    bool (*remove)(uint32_t index);
} t5_opds_api_v1;

const t5_opds_api_v1 *t5_opds_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
