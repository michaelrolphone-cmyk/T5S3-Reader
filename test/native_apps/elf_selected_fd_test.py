#!/usr/bin/env python3
"""Run production owned-fd reader, validator and complete dlmod/dlfcn on host.

Only hardware/platform calls and relocation execution are replaced. ASan/UBSan
cover the C path; explicit allocation and descriptor accounting catches leaks
when LeakSanitizer is unavailable under the executor's ptrace.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'lib/elf_loader/src/esp_elf.c').read_text()


def function(signature):
    start = SOURCE.index(signature)
    brace = SOURCE.index('{', start)
    depth = 0
    for end in range(brace, len(SOURCE)):
        depth += (SOURCE[end] == '{') - (SOURCE[end] == '}')
        if depth == 0:
            return SOURCE[start:end + 1]
    raise AssertionError('unterminated production function')


PREFIX = r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_elf.h"
#include "private/elf_platform.h"
#include "private/esp_elf_owned_fd.h"
#define TAG "ELF"
#define FS_PATH "/storage"
'''
STUBS = {
    'sdkconfig.h': '#define CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT 1\n',
    'freertos/FreeRTOS.h': r'''
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))
''',
    'freertos/task.h': r'''
#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t);
''',
    'freertos/semphr.h': r'''
#pragma once
#include "FreeRTOS.h"
typedef struct {int unused;} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *);
int xSemaphoreTake(SemaphoreHandle_t, TickType_t);
int xSemaphoreGive(SemaphoreHandle_t);
''',
    'esp_log.h': r'''
#pragma once
static inline void fixture_log(const char *tag, const char *fmt, ...){(void)tag;(void)fmt;}
#define ESP_LOGE(...) fixture_log(__VA_ARGS__)
#define ESP_LOGD(...) fixture_log(__VA_ARGS__)
#define ESP_LOGI(...) fixture_log(__VA_ARGS__)
#define ESP_LOGW(...) fixture_log(__VA_ARGS__)
''',
    'io.h': r'''
#define _GNU_SOURCE
#pragma once
#include <sys/types.h>
#include <stddef.h>
int fixture_open(const char *, int, ...);
int fixture_close(int);
off_t fixture_lseek(int, off_t, int);
ssize_t fixture_read(int, void *, size_t);
#define open fixture_open
#define close fixture_close
#define lseek fixture_lseek
#define read fixture_read
''',
}
with tempfile.TemporaryDirectory(prefix='selected-fd-elf-') as temporary:
    temp = Path(temporary)
    for name, content in STUBS.items():
        path = temp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    selected = '\n\n'.join(function(signature) for signature in (
        '__attribute__((weak)) bool esp_elf_admit_managed_app(',
        'int esp_elf_open(', 'void esp_elf_close(', 'int esp_elf_open_owned_fd('))
    (temp / 'reader.c').write_text(PREFIX + selected)
    command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare', '-g',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I' + str(temp), '-I' + str(ROOT / 'lib/elf_loader/include'),
               '-include', str(temp / 'io.h')]
    # Do not interpose host sanitizer/runtime dynamic-loading functions.
    for name in ('dlopen', 'dlclose', 'dlerror', 'dlsym', 'dllist'):
        command += [f'-D{name}=fixture_{name}']
    command += [str(temp / 'reader.c'),
                str(ROOT / 'lib/elf_loader/src/esp_elf_validate.c'),
                str(ROOT / 'lib/elf_loader/src/dlso/dlmod.c'),
                str(ROOT / 'lib/elf_loader/src/dlso/dlfcn.c'),
                str(ROOT / 'test/native_apps/elf_selected_fd_test.c'),
                '-o', str(temp / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(temp / 'test')], check=True, env=os.environ.copy())
