#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Firmware-private session over the already-admitted HalStorage backend. This
// does not select, discover, mount or grant authority to a provider.
bool native_app_source_begin(const char* path);
bool native_app_source_bind_owner(void);
// -1 rejects an invalid/missing selected session, 0 is legacy, 1 is selected.
int native_app_source_kind(const char* path);
int native_app_source_open(const char* path);
bool native_app_source_admit(void* path, const uint8_t* bytes, size_t size);
// Every loader exit checks I/O cleanup BEFORE releasing context or heap custody.
bool native_app_source_finish_io(void);
bool native_app_source_end(void);
bool native_app_source_retained(void);
void native_app_source_retain(void);
#ifdef __cplusplus
}
#endif
