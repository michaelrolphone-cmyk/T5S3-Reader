#!/usr/bin/env python3
"""Compile the production cache adapter against a host cache model."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
STUBS = r"""
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#define ESP_IDF_VERSION_VAL(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(4,4,7)
#define IRAM_ATTR
#define DRAM_ATTR
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define SOC_DROM_LOW 0x3c000000u
#define SOC_DROM_HIGH 0x3e000000u
#define SOC_IROM_LOW 0x42000000u
#define SOC_IROM_HIGH 0x44000000u
#define CONFIG_ELF_LOADER_LOAD_PSRAM 1
#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR 1
#define CONFIG_ELF_LOADER_CACHE_OFFSET 1
typedef int portMUX_TYPE;
typedef uint32_t TickType_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL test_enter
#define portEXIT_CRITICAL test_exit
#define pdMS_TO_TICKS(ms) (ms)
void test_enter(portMUX_TYPE *lock);
void test_exit(portMUX_TYPE *lock);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t delay);
void *heap_caps_malloc(size_t size, uint32_t caps);
void heap_caps_free(void *ptr);
int Cache_WriteBack_Addr(uint32_t addr, uint32_t size);
int Cache_Invalidate_Addr(uint32_t addr, uint32_t size);
"""

with tempfile.TemporaryDirectory(prefix="elf-cache-test-") as tmp:
    tmp = Path(tmp)
    (tmp / "cache_test_stubs.h").write_text(STUBS)
    for name in ("esp_idf_version.h", "esp_attr.h", "esp_heap_caps.h", "soc/soc.h",
                 "sdkconfig.h", "esp32s3/rom/cache.h", "freertos/FreeRTOS.h", "freertos/task.h"):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "cache_test_stubs.h"\n')
    binary = tmp / "cache_test"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter",  # Existing PSRAM allocator ignores its exec hint.
        f"-I{tmp}", f"-I{ROOT / 'lib/elf_loader/include'}",
        str(ROOT / "lib/elf_loader/src/esp_elf_adapter.c"),
        str(ROOT / "test/native_apps/elf_cache_sync_test.c"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
