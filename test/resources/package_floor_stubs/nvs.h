#pragma once

#include <cstddef>
#include <cstdint>

using esp_err_t = int;
using nvs_handle_t = uint32_t;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_INVALID_LENGTH = 0x110c;
constexpr int NVS_READWRITE = 1;

esp_err_t nvs_open(const char* name, int mode, nvs_handle_t* out);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* data, size_t* size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
