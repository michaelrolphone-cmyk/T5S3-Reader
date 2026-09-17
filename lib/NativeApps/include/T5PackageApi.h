#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_PACKAGE_API_VERSION 1u

typedef enum {
    T5_PACKAGE_RESULT_INSTALLED_INACTIVE = 0,
    T5_PACKAGE_RESULT_CANCELLED = 1,
    T5_PACKAGE_RESULT_UNTRUSTED = 2,
    T5_PACKAGE_RESULT_STALE_STAGE = 3,
    T5_PACKAGE_RESULT_PENDING_RECOVERY = 4,
    T5_PACKAGE_RESULT_FAILED = 5,
} t5_package_result_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    // False until a firmware-owned, exact kind/ID P-256 public-key policy is
    // provisioned. Keys and trust policy never come from a package or SD card.
    bool (*available)(void);
    // Only accepts a bounded /sd/Packages/inbox/*.risc path. Reauthenticates
    // the selected archive BEFORE presenting a firmware-owned physical-input
    // confirmation. Confirmation binds its exact signed-prefix fingerprint;
    // replacing the SD file while the dialog is open fails closed.
    // On success the caller must RETURN from app_main to yield the activity.
    bool (*install_request)(const char *sd_vfs_path, uint64_t cookie);
    // Read the result when the caller is resumed by the native activity.
    bool (*install_take_result)(t5_package_result_t *result, uint64_t *cookie);
} t5_package_api_v1;

const t5_package_api_v1 *t5_package_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
