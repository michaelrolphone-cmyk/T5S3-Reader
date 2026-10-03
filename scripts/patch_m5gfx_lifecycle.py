"""Patch pinned M5GFX 0.2.20 EPD ownership before dependency compilation.

Fail closed on source drift. The upstream destructor is empty despite owning
an update task, queue, DMA/LUT buffers and a PSRAM frame-state allocation.
"""
from pathlib import Path

MARKER = '// RiscRTE: joined EPD lifetime v1'


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('M5GFX lifecycle patch target changed: ' + old[:90])
    return text.replace(old, new, 1)


def patch_sources(header, source):
    if MARKER in header and MARKER in source:
        return header, source
    header = replace_once(header, '#include "Bus_EPD.h"', '#include "Bus_EPD.h"\n#include <atomic>')
    header = replace_once(header, '    virtual ~Panel_EPD(void);', '''    virtual ~Panel_EPD(void);
    // RiscRTE: joined EPD lifetime v1
    bool shutdown(uint32_t timeout_ms = 2000);
''')
    header = replace_once(header, '    TaskHandle_t _task_update_handle = nullptr;', '''    std::atomic<bool> _stop_requested{false};
    std::atomic<bool> _worker_done{true};
    TaskHandle_t _task_update_handle = nullptr;''')
    source = replace_once(source, '''  Panel_EPD::~Panel_EPD(void)
  {
  }''', '''  // RiscRTE: joined EPD lifetime v1
  Panel_EPD::~Panel_EPD(void)
  {
    // Never free an object that is still referenced by the worker/DMA.
    // Normal handoff calls shutdown first and retains it if that fails.
    if (!shutdown()) abort();
  }

  bool Panel_EPD::shutdown(uint32_t timeout_ms)
  {
    _stop_requested.store(true, std::memory_order_release);
    const TickType_t begun = xTaskGetTickCount();
    const TickType_t budget = pdMS_TO_TICKS(timeout_ms) + 1;
    while (!_worker_done.load(std::memory_order_acquire)) {
      if ((TickType_t)(xTaskGetTickCount() - begun) >= budget) return false;
      vTaskDelay(1);
    }
    // Worker has drained its final transfer and will never touch this again.
    _task_update_handle = nullptr;
    if (_update_queue_handle) { vQueueDelete(_update_queue_handle); _update_queue_handle = nullptr; }
    if (_buf) { heap_caps_free(_buf); _buf = nullptr; }
    if (_step_framebuf) { heap_caps_free(_step_framebuf); _step_framebuf = nullptr; }
    if (_dma_bufs[0]) { heap_caps_free(_dma_bufs[0]); _dma_bufs[0] = nullptr; }
    if (_dma_bufs[1]) { heap_caps_free(_dma_bufs[1]); _dma_bufs[1] = nullptr; }
    if (_lut_2pixel) { heap_caps_free(_lut_2pixel); _lut_2pixel = nullptr; }
    _display_busy = false;
    return true;
  }''')
    source = replace_once(source, '    auto cfg_detail = config_detail();', '''    if (!shutdown()) return false;
    _stop_requested.store(false, std::memory_order_release);
    auto cfg_detail = config_detail();''')
    source = replace_once(source, '    Panel_HasBuffer::init(use_reset);', '    if (!Panel_HasBuffer::init(use_reset)) return false;')
    source = replace_once(source, '    _update_queue_handle = xQueueCreate(8, sizeof(update_data_t));', '''    _update_queue_handle = xQueueCreate(8, sizeof(update_data_t));
    if (!_update_queue_handle) { shutdown(); return false; }''')
    source = replace_once(source, '    xTaskCreatePinnedToCore((TaskFunction_t)task_update, "epd", 4096, this, task_priority, &_task_update_handle, task_pinned_core);', '''    _worker_done.store(false, std::memory_order_release);
    if (xTaskCreatePinnedToCore((TaskFunction_t)task_update, "epd", 4096, this,
          task_priority, &_task_update_handle, task_pinned_core) != pdPASS) {
      _worker_done.store(true, std::memory_order_release);
      shutdown();
      return false;
    }''')
    source = replace_once(source, '''      TickType_t wait_tick = remain ? 0 : portMAX_DELAY;
      if (xQueueReceive(me->_update_queue_handle, &new_data, wait_tick)) {''', '''      if (me->_stop_requested.load(std::memory_order_acquire)) break;
      TickType_t wait_tick = remain ? 0 : pdMS_TO_TICKS(10) + 1;
      const bool received = xQueueReceive(me->_update_queue_handle, &new_data, wait_tick);
      if (me->_stop_requested.load(std::memory_order_acquire)) break;
      if (!received && !remain) continue;
      if (received) {''')
    source = replace_once(source, '''      if (remain == false) {
        bus->powerControl(false);
      }
    }
  }''', '''      if (remain == false) {
        bus->powerControl(false);
      }
    }
    bus->wait();
    bus->powerControl(false);
    me->_display_busy = false;
    // Final access to me: owner may destroy it immediately after this store.
    me->_worker_done.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
  }''')
    return header, source



WISP_MARKER = '// RiscRTE: static spatial refresh v1'


def patch_wisp_source(source):
    if WISP_MARKER in source:
        return source
    source = replace_once(source, '#include "lgfx/v1/misc/pixelcopy.hpp"',
                          '#include "lgfx/v1/misc/pixelcopy.hpp"\n#include "M5WispRefresh.h"\n#include <esp_timer.h>\n#include <esp_log.h>')
    source = replace_once(source, '          bool flg_fast = ( new_data.mode == epd_mode_t::epd_fastest)', '''          // RiscRTE: static spatial refresh v1
          bool spatial_done = false;
          if ((new_data.mode == epd_mode_t::epd_quality || new_data.mode == epd_mode_t::epd_text) &&
              panel_w == NativeVideoBootScrub::kWidth && memory_w == panel_w &&
              mh == NativeVideoBootScrub::kHeight && magni_h == 1) {
            struct WispHooks {
              std::atomic<bool>& stop;
              uint8_t* alloc(size_t bytes) {
                return static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
              }
              void free(uint8_t* p) { heap_caps_free(p); }
              uint32_t now() { return static_cast<uint32_t>(esp_timer_get_time()/1000); }
              void pause() { vTaskDelay(1); }
              bool stopped() { return stop.load(std::memory_order_acquire); }
            } hooks{me->_stop_requested};
            const uint32_t wisp_start = hooks.now();
            spatial_done = M5WispRefresh::run(bus, hooks, me->_buf, me->_step_framebuf,
                me->_dma_bufs[0], me->_dma_bufs[1], write_len,
                new_data.x, new_data.y, new_data.w, new_data.h, me->_lut_offset_table[new_data.mode]);
            if (spatial_done) ESP_LOGI("EPD_WISP", "static scrub + gray draw: %lu ms", static_cast<unsigned long>(hooks.now()-wisp_start));
            else if (!hooks.stopped()) ESP_LOGW("EPD_WISP", "spatial refresh unavailable; original waveform fallback");
            if (hooks.stopped()) break;
          }
          if (!spatial_done) {
          bool flg_fast = ( new_data.mode == epd_mode_t::epd_fastest)''')
    source = replace_once(source, '          } while (--h);\n\n          if (lgfx::micros() - usec >= 2048)',
                          '          } while (--h);\n          }  // Original fast path or safe waveform fallback.\n\n          if (lgfx::micros() - usec >= 2048)')
    return source


def patch_power_failure(source):
    marker = '// RiscRTE: refuse scan without confirmed panel power'
    if marker in source:
        return source
    return replace_once(source, '      bus->powerControl(true);',
        '      ' + marker + '\n      if (!bus->powerControl(true)) break;')


def patch_environment(env):
    if env.subst('$BOARD') != 't5s3-pro':
        return
    root = Path(env.subst('$PROJECT_LIBDEPS_DIR')) / env.subst('$PIOENV') / 'M5GFX/src/lgfx/v1/platforms/esp32'
    h, c = root/'Panel_EPD.hpp', root/'Panel_EPD.cpp'
    if not h.is_file() or not c.is_file():
        raise RuntimeError('Pinned M5GFX unavailable for required EPD lifecycle patch: ' + str(root))
    header, source = patch_sources(h.read_text(), c.read_text())
    source = patch_wisp_source(source)
    source = patch_power_failure(source)
    project = Path(env.subst('$PROJECT_DIR'))
    for companion in (project/'lib/hal/M5WispRefresh.h', project/'src/native/NativeVideoBootScrub.h'):
        destination = root/companion.name
        content = companion.read_text()
        if not destination.exists() or destination.read_text() != content:
            destination.write_text(content)
    if header != h.read_text(): h.write_text(header)
    if source != c.read_text(): c.write_text(source)


# Post extra scripts run after dependency installation, before compilation.
if 'Import' in globals():
    Import('env')
    patch_environment(env)
