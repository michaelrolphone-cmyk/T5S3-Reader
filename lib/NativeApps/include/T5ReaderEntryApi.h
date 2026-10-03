#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_READER_ENTRY_ABI_VERSION 1u
// The entry ELF owns this cooperative loop. Firmware callbacks retain the
// existing Reader activities, input, rendering and persistence implementation.
enum { T5_READER_ENTRY_CONTINUE = 0, T5_READER_ENTRY_HANDOFF = 1,
       T5_READER_ENTRY_STOP = 2 };
typedef struct {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t (*pump)(void);
} t5_reader_entry_api_v1;
const t5_reader_entry_api_v1* t5_reader_entry_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
