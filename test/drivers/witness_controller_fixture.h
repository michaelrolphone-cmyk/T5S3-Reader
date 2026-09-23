#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    unsigned reads, writes, used, read_timeout, write_timeout;
    bool blocked, write_error, close_error, detached;
    uint8_t transmitted[128];
} witness_controller_stats;
