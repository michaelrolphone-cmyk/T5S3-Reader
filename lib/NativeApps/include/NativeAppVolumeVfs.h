#pragma once
#include <esp_err.h>
#include <RiscStorageVolumeV1.h>

// Trusted firmware-only loader I/O binding, not an application SDK or provider
// selector. The caller supplies one already-admitted volume. Uses a pathless
// VFS registration ID, not a mount alias. Never discovers or mounts media.
// Register/unregister on the same exclusive owner used for loader sessions.
// Keep the exact API, context and provider grant mapped until unregister returns
// ESP_OK. Failed close, rejected-open cleanup or unregister retains this binding.
// Unregister retries only unreturned handles, never an admitted descriptor.
// Four descriptors are shared with the legacy /sd loader VFS. Files remain
// read-only and bounded to 52 bytes through 8 MiB. No launch/admission bypass:
// launch_elf_app and its capability/manifest gate still require legacy SD paths.
esp_err_t native_app_register_volume_vfs(const risc_storage_volume_api_v1* volume);
// Open an explicit volume-relative path and return a normal libc descriptor.
// Only read-only flags are admitted. Failed libc close may discard its fd even
// when the provider handle remains owned; then this binding stays retained.
int native_app_open_volume_file(const risc_storage_volume_api_v1* volume,
                                const char* path, int flags);
esp_err_t native_app_unregister_volume_vfs(const risc_storage_volume_api_v1* volume);
