#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_DRIVER_MANAGER_API_VERSION 1u
#define T5_DRIVER_ID_MAX 64u
#define T5_DRIVER_VERSION_MAX 32u
#define T5_DRIVER_CAPABILITY_MAX 64u

typedef struct {
    char id[T5_DRIVER_ID_MAX];
    char version[T5_DRIVER_VERSION_MAX];
    char capability[T5_DRIVER_CAPABILITY_MAX];
    uint32_t size_bytes;
} t5_driver_catalog_entry_t;

// Append-only recovery ABI. A retained download has kind DOWNLOAD and no ID;
// a staged generation has kind STAGE and a validated safe ID. No recovery API
// call activates drivers or grants capabilities. A successful inspection is
// informational, not permission to publish a package.
typedef enum {
    T5_DRIVER_RECOVERY_DOWNLOAD = 1,
    T5_DRIVER_RECOVERY_STAGE = 2,
} t5_driver_recovery_kind_t;

typedef enum {
    T5_DRIVER_RECOVERY_INCOMPLETE = 0,
    T5_DRIVER_RECOVERY_READY = 1,
    T5_DRIVER_RECOVERY_INVALID = 2,
    T5_DRIVER_RECOVERY_STALE = 3,
    T5_DRIVER_RECOVERY_MAPPED = 4,
    T5_DRIVER_RECOVERY_REQUIRES_REPAIR = 5,
} t5_driver_recovery_state_t;

typedef struct {
    char id[T5_DRIVER_ID_MAX];
    char candidate_version[T5_DRIVER_VERSION_MAX];
    uint64_t size_bytes;
    uint8_t kind;
    uint8_t state;
    bool can_retry;
    bool can_discard;
} t5_driver_recovery_entry_t;

// Progress reports are synchronous and observational: no background task,
// permission grant, cancellation, or package mutation is performed by the
// callback. Strings and event pointer are valid only during the call.
// total_bytes == 0 means that the transfer length is not known. The caller
// MUST NOT present percentages for stages that have no total byte count.
typedef enum {
    T5_DRIVER_INSTALL_RESOLVING = 1,
    T5_DRIVER_INSTALL_DEPENDENCY = 2,
    T5_DRIVER_INSTALL_CHECKING = 3,
    T5_DRIVER_INSTALL_METADATA = 4,
    T5_DRIVER_INSTALL_RECOVERY = 5,
    T5_DRIVER_INSTALL_DOWNLOADING = 6,
    T5_DRIVER_INSTALL_VERIFYING = 7,
    T5_DRIVER_INSTALL_PUBLISHING = 8,
    T5_DRIVER_INSTALL_INSTALLED = 9,
    T5_DRIVER_INSTALL_ALREADY_PRESENT = 10,
    T5_DRIVER_INSTALL_FAILED = 11,
} t5_driver_install_stage_t;

typedef struct {
    const char *package_id;
    // For DOWNLOADING this is the current package file basename; otherwise
    // it is NULL. No URL, credentials or arbitrary filesystem path is exposed.
    const char *file_name;
    uint8_t stage;
    uint64_t transferred_bytes;
    uint64_t total_bytes;
} t5_driver_install_event_t;

typedef void (*t5_driver_install_progress_t)(void *context,
                                              const t5_driver_install_event_t *event);

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*catalog_refresh)(void);
    uint32_t (*catalog_count)(void);
    bool (*catalog_get)(uint32_t index, t5_driver_catalog_entry_t *entry);
    bool (*installed_version_get)(const char *id, char *version, size_t capacity);
    bool (*install)(uint32_t index);
    // Optional, append-only. Check struct_size and all function pointers.
    // refresh enumerates ONLY manager-owned stages and the fixed .part file.
    // Indices are stable until the next refresh or recovery action.
    bool (*recovery_refresh)(void);
    uint32_t (*recovery_count)(void);
    bool (*recovery_get)(uint32_t index, t5_driver_recovery_entry_t *entry);
    // Retry rehashes and republishes a verified stage without network access.
    // Discard is destructive and MUST require explicit UI confirmation.
    bool (*recovery_retry)(uint32_t index);
    bool (*recovery_discard)(uint32_t index);
    // Optional, append-only. Called synchronously on the app's installing
    // task; callback and context are never stored after this call returns.
    // A NULL callback is permitted and behaves exactly like install().
    bool (*install_with_progress)(uint32_t index,
                                  t5_driver_install_progress_t progress,
                                  void *context);
} t5_driver_manager_api_v1;

const t5_driver_manager_api_v1 *t5_driver_manager_get_api(uint32_t version);

#ifdef __cplusplus
}
#endif
