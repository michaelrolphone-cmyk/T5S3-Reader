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
    uint8_t kind;
    uint8_t valid_installation;
    uint8_t reserved[2];
    char id[T5_PACKAGE_ID_MAX];
    char version[T5_PACKAGE_VERSION_MAX];
    char artifact[T5_PACKAGE_ARTIFACT_MAX];
} t5_installed_package_t;

typedef struct {
    t5_package_preview_t package;
    char archive[160];
} t5_package_catalog_row_t;

typedef void (*t5_package_progress_fn)(void *context,
                                       uint64_t downloaded_bytes,
                                       uint64_t total_bytes);

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
    // Additive v1 ABI tail. Inspect struct_size before calling either slot.
    // 'archive' is one .rte.zip basename under /Packages/Inbox, never a path.
    // All four kinds share the same bounded ordinary transaction. These
    // operations never delete the archive or load/activate its ELF.
    bool (*preview_archive)(const char *archive, t5_package_preview_t *out);
    bool (*install_archive)(const char *archive);
    // Generic immutable-release index, not the legacy app or USB catalogs.
    // online_refresh fetches one bounded package-catalog.json and pins its
    // declared release tag; count/get are filtered to the caller's permitted
    // kinds. Indexes are valid only until the next refresh. No metadata alone
    // authorizes an install or ELF execution: online_install independently
    // checks the selected archive SHA, retained manifest and transaction.
    bool (*online_refresh)(void);
    uint32_t (*online_count)(void);
    bool (*online_get)(uint32_t index, t5_package_catalog_row_t *out);
    bool (*online_install)(uint32_t index);

    // Append-only U1 ABI tail. Keep archive/online slots above at their
    // deployed offsets; installed inventory and replacement follow them.
    bool (*installed_refresh)(void);
    uint32_t (*installed_count)(void);
    bool (*installed_get)(uint32_t index, t5_installed_package_t *out);
    bool (*replace)(const char *folder);
    // Optional generic release-install UX tail. Existing callers can keep
    // using online_install; newer package UIs get bounded download progress
    // and a short failure reason without changing the deployed prefix.
    bool (*online_install_with_progress)(uint32_t index,
                                         t5_package_progress_fn progress,
                                         void *context);
    bool (*online_last_error)(char *out, size_t capacity);
} t5_package_manager_api_v1;

const t5_package_manager_api_v1 *t5_package_manager_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
