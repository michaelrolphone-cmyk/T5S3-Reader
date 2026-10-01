#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_ARCHIVE_ZIP_API_V1 1u
#define RISC_ARCHIVE_ZIP_MAX_BYTES 131072u
#define RISC_ARCHIVE_ZIP_CHUNK 512u
#define RISC_ARCHIVE_ZIP_MAX_ENTRIES 17u
#define RISC_ARCHIVE_ZIP_NAME_BYTES 128u
/* Stored ZIP subset, copied bytes only. No path or filesystem authority is
 * conveyed to the service. A caller reads its scoped input stream in bounded
 * chunks; extracted bytes go back to caller-owned buffers/scoped output streams.
 * Each job is bound to one provider generation and expires after60 seconds.
 * Calls are serialized; a concurrent/reentrant call returns BUSY.
 * The capability lease must remain live through close. No pointers are retained.
 */
typedef uint64_t risc_archive_zip_job;
enum {
  RISC_ZIP_OK=0, RISC_ZIP_EOF=1, RISC_ZIP_INVALID=-1, RISC_ZIP_BUSY=-2,
  RISC_ZIP_LIMIT=-3, RISC_ZIP_UNSUPPORTED=-4, RISC_ZIP_CORRUPT=-5,
  RISC_ZIP_TIMEOUT=-6, RISC_ZIP_STALE=-7
};
typedef struct {
  char name[RISC_ARCHIVE_ZIP_NAME_BYTES];
  uint32_t size_bytes;
} risc_archive_zip_entry_v1;
typedef struct {
  uint32_t api_version, struct_size;
  int32_t (*begin)(uint32_t archive_bytes, risc_archive_zip_job*);
  int32_t (*append)(risc_archive_zip_job, const void*, uint32_t size);
  int32_t (*seal)(risc_archive_zip_job);
  int32_t (*count)(risc_archive_zip_job, uint32_t*);
  int32_t (*entry)(risc_archive_zip_job, uint32_t index, risc_archive_zip_entry_v1*);
  int32_t (*read)(risc_archive_zip_job, uint32_t index, uint32_t offset,
                  void*, uint32_t capacity, uint32_t*);
  int32_t (*close)(risc_archive_zip_job);
} risc_archive_zip_api_v1;
#ifdef __cplusplus
}
#endif
