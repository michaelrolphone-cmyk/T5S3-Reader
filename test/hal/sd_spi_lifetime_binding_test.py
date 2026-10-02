#!/usr/bin/env python3
"""Production binding guard for the port's task/module/pin retention barriers."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def source(p): return (ROOT/p).read_text()
def before(text,first,later): assert text.index(first)<text.index(later),(first,later)
text=source('lib/NativeApps/src/NativeAppLauncher.c').split('close_module:',1)[1]
before(text,'risc_runtime_retention_guard();','module_fini();')
before(text,'risc_runtime_retention_guard();','native_app_memory_end();')
text=source('src/runtime/drivers/ProviderModuleV2.cpp').split('bool ModuleV2::closeMapped()',1)[1]
before(text,'risc_runtime_retention_guard();','esp_elf_deinit(')
text=source('src/runtime/drivers/ProviderGraphV2.cpp').split('GraphV2::~GraphV2()',1)[1]
before(text,'risc_runtime_retention_guard();','std::abort()')
text=source('lib/elf_loader/src/dlso/dlfcn.c').split('int dlclose(',1)[1]
assert 'risc_runtime_retention_guard();' in text
for p in ('lib/Board_T5S3/BoardT5S3.cpp','lib/Board_EPD47/BoardEPD47.cpp'):
 text=source(p)
 for method in ('prepareSdBus','deinitForSleep'):
  body=text.split('void '+method+'() {',1)[1]
  before(body,'risc_sd_spi_guard();','pinMode(')
flags=source('platformio.ini')
for name in ('vTaskDelete','pinMode','digitalWrite','gpio_set_level','gpio_config'):
 assert '--wrap='+name in flags
assert 'pre:scripts/patch_sd_spi_fault.py' in flags and '-DRISCRTE_SD_SPI_FAULT_PORT=1' in flags
print('Production retention bindings: task deletion, module/context disposal, board pin/sleep and raw compatibility wrappers PASS')
