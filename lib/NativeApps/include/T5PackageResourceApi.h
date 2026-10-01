#pragma once
#include <T5StreamApi.h>
#ifdef __cplusplus
extern "C" {
#endif
#define T5_PACKAGE_RESOURCE_API_VERSION 1u
/* Read-only declared resources of this invocation's installed package.
 * No arbitrary root/ID, executable, metadata, directory or mutable data access.
 * Handles use the existing stream ABI: bounded reads, seek, close and pipes.
 * The host pins the installed generation until each stream is retired.
 * Legacy loose apps have no installed package resource authority.
 */
typedef struct {
  uint32_t api_version, struct_size;
  t5_stream_result_t (*open)(const char *relative_name, t5_stream_t *out);
} t5_package_resource_api_v1;
const t5_package_resource_api_v1 *t5_package_resource_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
