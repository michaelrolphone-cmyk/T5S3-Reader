#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_PACKAGE_MANAGER_API_VERSION 1u
#define T5_PACKAGE_ID_MAX 64u
#define T5_PACKAGE_VERSION_MAX 32u
#define T5_PACKAGE_ARTIFACT_MAX 128u

typedef enum {
    T5_PACKAGE_APPLICATION = 0,
    T5_PACKAGE_DRIVER = 1,
    T5_PACKAGE_SERVICE = 2,
    T5_PACKAGE_PROVIDER = 3,
} t5_package_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t valid_installation;
    uint8_t install_allowed;
    uint8_t reserved;
    char id[T5_PACKAGE_ID_MAX];
    char version[T5_PACKAGE_VERSION_MAX];
    char artifact[T5_PACKAGE_ARTIFACT_MAX];
    char installed_version[T5_PACKAGE_VERSION_MAX];
} t5_package_preview_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    // 'folder' is exactly ONE safe basename under /Packages/Inbox. An app
    // cannot supply arbitrary SD paths or request privilege activation.
    bool (*preview)(const char *folder, t5_package_preview_t *out);
    // Re-preview, reparse, rehash, stage and publish an ordinary package.
    // A true result does NOT load the ELF or grant a hardware capability.
    bool (*install)(const char *folder);
    // Uninstall a canonical package selected by its kind and ID; requires
    // unmapping first. Legacy flat app and old-layout driver data is retained.
    bool (*uninstall)(uint8_t kind, const char *id);
} t5_package_manager_api_v1;

const t5_package_manager_api_v1 *t5_package_manager_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
