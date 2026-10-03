#!/usr/bin/env python3
"""Stage the existing pinned M5 Reader engine for the external display ELF.

Waveforms, grayscale conversion and Wisp refresh remain the existing sources.
Only lifetime, bounded failure propagation and CPU imports change here.
"""
from pathlib import Path
import shutil
from patch_m5gfx_lifecycle import patch_sources,patch_wisp_source,patch_power_failure,replace_once as once


def prepare(source: Path,destination: Path,root: Path):
    shutil.copytree(source,destination,dirs_exist_ok=True)
    directory=destination/'lgfx/v1/platforms/esp32'
    h=(directory/'Panel_EPD.hpp').read_text();c=(directory/'Panel_EPD.cpp').read_text()
    h,c=patch_sources(h,c);c=patch_power_failure(patch_wisp_source(c))
    h=once(h,'#include <atomic>','#include <atomic>\n#include "RiscCpuWorkerV2.h"')
    h=once(h,'    TaskHandle_t _task_update_handle = nullptr;',
        '    risc_cpu_worker_v2 _task_update_handle = 0;\n    std::atomic<bool> _display_failed{false};\n    std::atomic<uint32_t> _submitted{0}, _completed{0};')
    h=once(h,'    bool shutdown(uint32_t timeout_ms = 2000);',
        '    bool shutdown(uint32_t timeout_ms = 2000);\n    bool failed() { return _display_failed.load(std::memory_order_acquire) || getBusEPD()->failed(); }\n    bool completed() { return !failed() && _submitted.load(std::memory_order_acquire)==_completed.load(std::memory_order_acquire); }')
    h=once(h,'    struct update_data_t {','    struct update_data_t {\n      uint32_t serial;')
    a=c.index('    const TickType_t begun = xTaskGetTickCount();');b=c.index('    if (_update_queue_handle)',a)
    c=c[:a]+'''    if (_task_update_handle) {
      if (risc_cpu_worker_join_v2(_task_update_handle,timeout_ms) ||
          risc_cpu_worker_release_v2(_task_update_handle)) return false;
      _task_update_handle = 0;
    }
    // Join proves no future worker access, but DMA/IRQ may remain after a
    // timed-out scan. Retain every pixel/LUT buffer until bus teardown succeeds.
    if (_bus) {
      auto bus = getBusEPD();
      bus->release();
      if (!bus->released()) return false;
    }
'''+c[b:]
    c=once(c,'    _stop_requested.store(false, std::memory_order_release);',
        '    _stop_requested.store(false, std::memory_order_release);\n    _display_failed.store(false,std::memory_order_release);\n    _submitted=0;_completed=0;')
    c=once(c,'''    if (xTaskCreatePinnedToCore((TaskFunction_t)task_update, "epd", 4096, this,
          task_priority, &_task_update_handle, task_pinned_core) != pdPASS) {''',
        '''    if (risc_cpu_worker_start_v2([](void* p) { task_update(static_cast<Panel_EPD*>(p)); },
          this,4096,task_priority,task_pinned_core,&_task_update_handle) != RISC_CPU_WORKER_OK) {''')
    c=once(c,'    vTaskDelete(nullptr);','    return; // Resident trampoline publishes final completion after return.')
    c=once(c,'    while (_display_busy) { vTaskDelay(1); }',
        '''    const int64_t began=esp_timer_get_time();
    unsigned waits=0;
    while (!completed()) {
      if (failed() || ++waits>3000 || esp_timer_get_time()-began>=3000000) {
        _display_failed.store(true,std::memory_order_release);
        _stop_requested.store(true,std::memory_order_release);return;
      }
      vTaskDelay(1);
    }''')
    # The owned framebuffer is PSRAM; no raw ROM cache API is imported.
    a=c.index('#if __has_include(<esp_cache.h>)');b=c.index('namespace lgfx',a)
    c=c[:a]+'#include "RiscCpuCacheV2.h"\n'+c[b:]
    a=c.index('#if defined ( LGFX_USE_CACHE_WRITEBACK_ADDR )');b=c.index('//----------------------------------------------------------------------------',a)
    c=c[:a]+'''  static bool cacheWriteBack(const void* ptr,uint32_t size) {
    return !size || isEmbeddedMemory(ptr) || risc_cpu_cache_writeback_v2((uintptr_t)ptr,size)==0;
  }

'''+c[b:]
    c=once(c,'    cacheWriteBack(&_buf[y * _cfg.panel_width >> 1], h * _cfg.panel_width >> 1);',
        '''    if (!cacheWriteBack(&_buf[upd.y * _cfg.panel_width >> 1],upd.h * _cfg.panel_width >> 1)) {
      _display_failed.store(true,std::memory_order_release);
      _stop_requested.store(true,std::memory_order_release);return;
    }''')
    c=once(c,'    if (res)\n    {',
        '''    if (!res) {
      _display_failed.store(true,std::memory_order_release);
      _stop_requested.store(true,std::memory_order_release);return;
    }
    if (res)
    {''')
    c=c.replace('heap_caps_aligned_alloc(16,','heap_caps_aligned_calloc(16,1,')
    c=once(c,'      me->_display_busy = remain;',
        '''      if (bus->failed()) { me->_display_failed.store(true,std::memory_order_release);break; }
      me->_display_busy = remain;''')
    c=once(c,'              std::atomic<bool>& stop;',
        '              std::atomic<bool>& stop;\n              Bus_EPD* bus;')
    c=once(c,'bool stopped() { return stop.load(std::memory_order_acquire); }',
        'bool stopped() { return stop.load(std::memory_order_acquire) || bus->failed(); }')
    c=once(c,'} hooks{me->_stop_requested};','} hooks{me->_stop_requested,bus};')
    c=once(c,'    update_data_t upd;',
        '    if (failed() || _submitted.load(std::memory_order_acquire)==UINT32_MAX) {\n      _display_failed.store(true,std::memory_order_release);return;\n    }\n    update_data_t upd;\n    upd.serial=_submitted.fetch_add(1,std::memory_order_release)+1;')
    c=once(c,'    bool remain = false;','    bool remain = false;\n    uint32_t last_applied=0;')
    c=once(c,'        for (;;) {','        for (unsigned batch=0;batch<8;++batch) {')
    c=once(c,'          if (lgfx::micros() - usec >= 2048) {',
        '          last_applied=new_data.serial;\n          if (lgfx::micros() - usec >= 2048) {')
    c=once(c,'          const update_data_t prev_data = new_data;\n          bool result;\n          while (true == (result = xQueueReceive(me->_update_queue_handle, &new_data, 0))) {\n            if (prev_data != new_data) { break; }\n          }\n          if (result == false) { break; }',
        '          if (batch==7 || xQueueReceive(me->_update_queue_handle,&new_data,0)!=pdTRUE)break;')
    c=once(c,'      if (remain == false) {\n        bus->powerControl(false);\n      }',
        '      if (remain == false) {\n        if (!bus->powerControl(false) || bus->failed()) {\n          me->_display_failed.store(true,std::memory_order_release);break;\n        }\n        me->_completed.store(last_applied,std::memory_order_release);\n      }')
    (directory/'Panel_EPD.hpp').write_text(h);(directory/'Panel_EPD.cpp').write_text(c)
    for companion in (root/'lib/hal/M5WispRefresh.h',root/'src/native/NativeVideoBootScrub.h'):
        shutil.copyfile(companion,directory/companion.name)

    h=(directory/'Bus_EPD.h').read_text();c=(directory/'Bus_EPD.cpp').read_text()
    h='#include <atomic>\n'+h
    h=once(h,'  bool init(void) override;',
        '''  bool init(void) override;
  void release(void) override;
  virtual bool released() const { return !_io_handle && !_i80_bus_handle; }
  bool failed() const { return _faulted.load(std::memory_order_acquire); }
''')
    h=once(h,'  volatile bool _bus_busy = false;',
        '  std::atomic<bool> _bus_busy{false};\n  std::atomic<bool> _faulted{false};')
    c=once(c,'void Bus_EPD::wait(void) { while (_bus_busy) taskYIELD(); }',
        '''void Bus_EPD::wait(void) {
  const int64_t began=esp_timer_get_time();unsigned waits=0;
  while (_bus_busy) {
    if (failed() || ++waits>100 || esp_timer_get_time()-began>=100000) {
      _faulted.store(true,std::memory_order_release);return;
    }
    vTaskDelay(1);
  }
}
void Bus_EPD::release(void) {
  if (_io_handle) {
    if (esp_lcd_panel_io_del(_io_handle)!=ESP_OK)return;
    _io_handle=nullptr;
  }
  if (_i80_bus_handle) {
    if (esp_lcd_del_i80_bus(_i80_bus_handle)!=ESP_OK)return;
    _i80_bus_handle=nullptr;
  }
}
''')
    c=c.replace('  while (_bus_busy) { taskYIELD(); }','  wait(); if (failed()) return;')
    c=once(c,'  esp_lcd_panel_io_tx_color(_io_handle, -1, data, length);',
        '''  if (esp_lcd_panel_io_tx_color(_io_handle,-1,data,length)!=ESP_OK)
    _faulted.store(true,std::memory_order_release);''')
    c=once(c,'  _bus_busy = false;\n  _pwr_on = false;',
        '''  if (_io_handle || _i80_bus_handle) return false;
  _faulted.store(false,std::memory_order_release);
  _bus_busy = false;
  _pwr_on = false;''')
    import re
    c=re.sub(r'lgfx::pinMode\((_config\.pin_(?:spv|ckv|sph|le|cl|data\[i\])), lgfx::pin_mode_t::output\);',r'gpio_set_direction(static_cast<gpio_num_t>(\1), GPIO_MODE_OUTPUT);',c)
    c=c.replace('  lgfx::pinMode(_config.pin_oe, lgfx::pin_mode_t::output);\n','')
    c=c.replace('  lgfx::pinMode(_config.pin_pwr, lgfx::pin_mode_t::output);\n','')
    c=once(c,'  bus_config.dc_gpio_num = (gpio_num_t)_config.pin_pwr; //<= dummy setting.',
        '  bus_config.dc_gpio_num = -1; // No D/C wire; never touch radio CS46.')
    (directory/'Bus_EPD.h').write_text(h);(directory/'Bus_EPD.cpp').write_text(c)
    p=destination/'lgfx/v1/panel/Panel_Device.cpp';c=p.read_text()
    c=once(c,'    static Bus_NULL nullobj;\n    _bus = bus ? bus : &nullobj;',
        '    _bus = bus; // External provider always binds an explicit bus before init.')
    c=once(c,'    _bus->init();\n    rst_control(true);','    if (!_bus || !_bus->init()) return false;\n    rst_control(true);')
    c=c.replace('lgfx::pinMode(pin , pin_mode_t::output);','gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);').replace('lgfx::pinMode(pin, pin_mode_t::output);','gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);')
    p.write_text(c)

if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('destination',type=Path)
    args=ap.parse_args();prepare(args.source,args.destination,Path(__file__).resolve().parents[1])
