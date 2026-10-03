#pragma once

#include <cstddef>

struct esp_partition_t {
    size_t size;
};

extern bool sd_firmware_partition_available;
extern size_t sd_firmware_partition_size;

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *partition);
