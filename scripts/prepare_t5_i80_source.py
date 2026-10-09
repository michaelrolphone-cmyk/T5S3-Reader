#!/usr/bin/env python3
"""Stage bounded IDF 4.4.7 i80 code for the external display build.

Not applied to resident firmware or a shared SDK. The provider retains failed
startup/cleanup handles; it must not unload this code until deletion succeeds.
"""
from pathlib import Path
import hashlib
import argparse
import re


def once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('Pinned i80 source drift: '+old[:90])
    return source.replace(old,new,1)


def prepare(source):
    marker='// RiscRTE external i80 bounded lifecycle v1'
    if hashlib.sha256(source.encode()).hexdigest() != 'bd513023aa79c285a6af275203f8374259d20e47339df48fca20a7f6414075d9':
        raise ValueError('Unrecognized IDF 4.4.7 i80 source')
    source=once(source,'static const char *TAG',marker+'\nstatic const char *TAG')
    source=once(source,'    int bus_id;            // Bus ID, index from 0',
                '''    int irq_core;
    bool dma_disconnected;
    volatile unsigned isr_active;
    bool closing;
    int bus_id;            // Bus ID, index from 0''')
    source=once(source,'    esp_err_t ret = ESP_OK;\n    esp_lcd_i80_bus_t *bus = NULL;',
        '    esp_err_t ret = ESP_OK;\n    if (ret_bus) *ret_bus = NULL;\n    ESP_RETURN_ON_FALSE(!xPortInIsrContext() && bus_config && ret_bus &&\n        bus_config->bus_width == 8 && bus_config->max_transfer_bytes > 0 &&\n        bus_config->max_transfer_bytes <= 4096 && bus_config->dc_gpio_num == -1 &&\n        bus_config->wr_gpio_num == 4 &&\n        bus_config->data_gpio_nums[0] == 5 && bus_config->data_gpio_nums[1] == 6 &&\n        bus_config->data_gpio_nums[2] == 7 && bus_config->data_gpio_nums[3] == 15 &&\n        bus_config->data_gpio_nums[4] == 16 && bus_config->data_gpio_nums[5] == 17 &&\n        bus_config->data_gpio_nums[6] == 18 && bus_config->data_gpio_nums[7] == 8,\n        ESP_ERR_INVALID_ARG, TAG, "invalid raw EPD bus configuration");\n    esp_lcd_i80_bus_t *bus = NULL;')
    source=once(source,'    bus->bus_id = bus_id;',
        '    bus->bus_id = bus_id;\n    bus->irq_core = xPortGetCoreID();')
    source=once(source,'    bool bus_exclusive = false;',
        '    bool bus_exclusive = false;\n    if (ret_io) *ret_io = NULL;\n    ESP_RETURN_ON_FALSE(!xPortInIsrContext() && bus && io_config && ret_io &&\n        !bus->closing && LIST_EMPTY(&bus->device_list) && io_config->pclk_hz &&\n        io_config->trans_queue_depth > 0 && io_config->trans_queue_depth <= 4 &&\n        io_config->cs_gpio_num == 41,\n        ESP_ERR_INVALID_ARG, TAG, "invalid raw EPD panel configuration");')
    # Bound each synchronous queue wait. This profile permits one panel and
    # queue depth <= 4; callers retain DMA buffers after any returned failure.
    source=source.replace('portMAX_DELAY','(pdMS_TO_TICKS(100) + 1)')
    source=once(source,'    xQueueSend(i80_device->trans_queue, &trans_desc, (pdMS_TO_TICKS(100) + 1));',
                '''    ESP_RETURN_ON_FALSE(xQueueSend(i80_device->trans_queue, &trans_desc, (pdMS_TO_TICKS(100) + 1)) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "queue admission timeout");''')
    # GPIO46 is the radio's chip select, not a spare display control. The raw
    # EPD sends no commands, so it has no physical D/C pin. Support -1 only
    # in this provider-private copy; its public capability never exposes i80.
    source=once(source,'(bus_config->wr_gpio_num >= 0) && (bus_config->dc_gpio_num >= 0)',
                '(bus_config->wr_gpio_num >= 0) && (bus_config->dc_gpio_num >= -1)')
    old='''    gpio_set_direction(bus_config->dc_gpio_num, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(bus_config->dc_gpio_num, lcd_periph_signals.buses[bus_id].dc_sig, false, false);
    gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[bus_config->dc_gpio_num], PIN_FUNC_GPIO);'''
    source=once(source,old,'    if (bus_config->dc_gpio_num >= 0) {\n'+old+'\n    }')
    # A dummy start has no DMA buffer. If it does not finish, return a retained
    # bus handle for explicit cleanup rather than leak it or block forever.
    source=source.replace('static void lcd_periph_trigger_quick_trans_done_event(',
                          'static esp_err_t lcd_periph_trigger_quick_trans_done_event(')
    spin='    while (!(lcd_ll_get_interrupt_status(bus->hal.dev) & LCD_LL_EVENT_TRANS_DONE)) {}'
    if source.count(spin)!=2: raise ValueError('Pinned i80 wait count changed')
    bounded='''    const TickType_t wait_began = xTaskGetTickCount();
    unsigned wait_polls = 0;
    while (!(lcd_ll_get_interrupt_status(bus->hal.dev) & LCD_LL_EVENT_TRANS_DONE)) {
        if (++wait_polls > 100 || (TickType_t)(xTaskGetTickCount() - wait_began) >= pdMS_TO_TICKS(100) + 1)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }'''
    source=source.replace(spin,bounded)
    source=once(source,bounded+'\n}\n\nstatic void lcd_start_transaction',
                bounded+'\n    return ESP_OK;\n}\n\nstatic void lcd_start_transaction')
    source=once(source,'    lcd_periph_trigger_quick_trans_done_event(bus);',
                '''    ret = lcd_periph_trigger_quick_trans_done_event(bus);
    if (ret != ESP_OK) {
        LIST_INIT(&bus->device_list);
        bus->spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        *ret_bus = bus;
        return ret; // Caller retains this partially initialized controller.
    }''')
    source=once(source,'    bus->bus_id = -1;',
                '    bus->bus_id = -1;\n    bus->dma_disconnected = true;\n    LIST_INIT(&bus->device_list);\n    bus->spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;')
    start=source.index('err:\n    if (bus) {')
    end=source.index('\nesp_err_t esp_lcd_del_i80_bus(',start)
    source=source[:start]+'''err:
    if (bus) {
        if (bus->bus_id < 0) {
            free(bus->format_buffer);
            free(bus);
        } else {
            *ret_bus = bus; // Explicit retry owns every partial resource.
        }
    }
    return ret;
}
'''+source[end:]
    source=once(source,'    gdma_connect(bus->dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));',
                '''    ESP_RETURN_ON_ERROR(gdma_connect(bus->dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0)), TAG, "connect DMA failed");
    bus->dma_disconnected = false;''')
    source=once(source,'    gdma_apply_strategy(bus->dma_chan, &strategy_config);',
                '    ESP_RETURN_ON_ERROR(gdma_apply_strategy(bus->dma_chan, &strategy_config), TAG, "DMA strategy failed");')
    source=once(source,'    gdma_set_transfer_ability(bus->dma_chan, &ability);',
                '    ESP_RETURN_ON_ERROR(gdma_set_transfer_ability(bus->dma_chan, &ability), TAG, "DMA ability failed");')
    source=once(source,'''err:
    if (bus->dma_chan) {
        gdma_del_channel(bus->dma_chan);
    }
    return ret;''','''err:
    return ret; // Factory caller retains the bus and any partial DMA channel.''')
    # Keep failed cleanup handles usable. The generic graph's false quiesce
    # must mean the provider image, IRQ state and DMA allocations remain valid.
    start=source.index('esp_err_t esp_lcd_del_i80_bus(')
    end=source.index('\nesp_err_t esp_lcd_new_panel_io_i80(',start)
    source=source[:start]+'''esp_err_t esp_lcd_del_i80_bus(esp_lcd_i80_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "invalid bus");
    ESP_RETURN_ON_FALSE(LIST_EMPTY(&bus->device_list), ESP_ERR_INVALID_STATE, TAG, "device list not empty");
    ESP_RETURN_ON_FALSE(!xPortInIsrContext() && xPortGetCoreID() == bus->irq_core,
                        ESP_ERR_INVALID_STATE, TAG, "teardown requires allocating IRQ core");
    bus->closing = true;
    lcd_ll_enable_interrupt(bus->hal.dev, LCD_LL_EVENT_TRANS_DONE, false);
    if (bus->intr) {
        ESP_RETURN_ON_ERROR(esp_intr_disable(bus->intr), TAG, "disable IRQ failed");
        // Same-core esp_intr_free removes the handler under its allocator
        // critical section, after any previously preempting ISR has returned.
        // Avoid the SDK's unbounded cross-core IPC path. The counter alone
        // would not prove that an ISR epilogue finished executing ELF code.
        ESP_RETURN_ON_ERROR(esp_intr_free(bus->intr), TAG, "free IRQ failed");
        bus->intr = NULL;
    }
    lcd_ll_stop(bus->hal.dev);
    if (bus->dma_chan) {
        if (!bus->dma_disconnected) {
            ESP_RETURN_ON_ERROR(gdma_disconnect(bus->dma_chan), TAG, "disconnect DMA failed");
            bus->dma_disconnected = true;
        }
        ESP_RETURN_ON_ERROR(gdma_del_channel(bus->dma_chan), TAG, "delete DMA failed");
        bus->dma_chan = NULL;
    }
    if (bus->pm_lock) {
        ESP_RETURN_ON_ERROR(esp_pm_lock_delete(bus->pm_lock), TAG, "free clock lock failed");
        bus->pm_lock = NULL;
    }
    lcd_com_remove_device(LCD_COM_DEVICE_TYPE_I80, bus->bus_id);
    periph_module_disable(lcd_periph_signals.buses[bus->bus_id].module);
    free(bus->format_buffer);
    free(bus);
    return ESP_OK;
}
''' +source[end:]
    source=once(source,'    esp_lcd_i80_bus_t *bus = (esp_lcd_i80_bus_t *)args;',
                '''    esp_lcd_i80_bus_t *bus = (esp_lcd_i80_bus_t *)args;
    __atomic_add_fetch(&bus->isr_active, 1, __ATOMIC_ACQUIRE);''')
    source=once(source,'    if (need_yield) {',
                '    __atomic_sub_fetch(&bus->isr_active, 1, __ATOMIC_RELEASE);\n    if (need_yield) {')
    # Raw EPD uses only PLL160M and no command transactions. Do not smuggle
    # firmware PM/XTAL APIs into the provider or silently skip an enabled PM port.
    source=source.replace('#include "esp_pm.h"', '#include "esp_pm.h"\n#include "RiscCpuCacheV2.h"\n#if CONFIG_PM_ENABLE || CONFIG_LCD_ISR_IRAM_SAFE\n#error "External raw EPD requires fixed PLL clock and non-IRAM IRQ admission"\n#endif')
    start=source.index('static esp_err_t lcd_i80_select_periph_clock(',source.index('static esp_err_t panel_io_i80_tx_color(',source.index('esp_err_t esp_lcd_new_i80_bus(')))
    end=source.index('\nstatic esp_err_t lcd_i80_init_dma_link(',start)
    source=source[:start]+'static esp_err_t lcd_i80_select_periph_clock(esp_lcd_i80_bus_handle_t bus, lcd_clock_source_t clk_src)\n{\n    ESP_RETURN_ON_FALSE(clk_src == LCD_CLK_SRC_PLL160M, ESP_ERR_NOT_SUPPORTED, TAG, "fixed EPD PLL clock required");\n    lcd_ll_select_clk_src(bus->hal.dev, clk_src);\n    lcd_ll_set_group_clock_coeff(bus->hal.dev, LCD_PERIPH_CLOCK_PRE_SCALE, 0, 0);\n    bus->resolution_hz = 160000000 / LCD_PERIPH_CLOCK_PRE_SCALE;\n    return ESP_OK;\n}\n'+source[end:]
    source=re.sub(r'^[ \t]*if \(bus->pm_lock\) \{[^{}]*\}\n','',source,flags=re.M)
    source=re.sub(r'^.*esp_pm_lock_handle_t pm_lock;.*\n','',source,flags=re.M)
    # Both engines use tx_color(...,-1,...). Refuse commands explicitly.
    start=source.index('static esp_err_t panel_io_i80_tx_param(',source.index('esp_err_t esp_lcd_new_i80_bus('))
    end=source.index('\nstatic esp_err_t panel_io_i80_tx_color(',start)
    source=source[:start]+'static esp_err_t panel_io_i80_tx_param(esp_lcd_panel_io_t *io, int lcd_cmd, const void *param, size_t param_size)\n{\n    (void)io; (void)lcd_cmd; (void)param; (void)param_size;\n    return ESP_ERR_NOT_SUPPORTED;\n}\n'+source[end:]
    source=once(source,'    assert(color_size <= (bus->num_dma_nodes * DMA_DESCRIPTOR_BUFFER_MAX_SIZE) && "color bytes too long, enlarge max_transfer_bytes");',
        '    ESP_RETURN_ON_FALSE(!bus->closing && !xPortInIsrContext() && lcd_cmd == -1 &&\n        color && color_size && color_size <= 4096 &&\n        color_size <= bus->num_dma_nodes * DMA_DESCRIPTOR_BUFFER_MAX_SIZE,\n        ESP_ERR_INVALID_ARG, TAG, "invalid raw pixel transaction");')
    source=once(source,'        Cache_WriteBack_Addr((uint32_t)color, color_size);',
        '        ESP_RETURN_ON_FALSE(risc_cpu_cache_writeback_v2((uintptr_t)color,color_size) == 0,\n                            ESP_FAIL, TAG, "pixel cache writeback failed");')
    source=once(source,'    // wait all pending transaction to finish',
        '    ESP_RETURN_ON_FALSE(!xPortInIsrContext() && xPortGetCoreID() == bus->irq_core,\n                        ESP_ERR_INVALID_STATE, TAG, "IO teardown requires allocating IRQ core");\n    // wait all pending transaction to finish')
    # A completed transaction may have enqueued its completion while the ISR
    # still owns cur_device. Remove that IRQ before deleting any IO/queue state.
    source=once(source,'    // remove from device list',
        '    ESP_RETURN_ON_FALSE(!xPortInIsrContext() && xPortGetCoreID() == bus->irq_core,\n                        ESP_ERR_INVALID_STATE, TAG, "IO teardown requires allocating IRQ core");\n    bus->closing = true;\n    lcd_ll_enable_interrupt(bus->hal.dev,LCD_LL_EVENT_TRANS_DONE,false);\n    if (bus->intr) {\n        ESP_RETURN_ON_ERROR(esp_intr_disable(bus->intr),TAG,"disable IO IRQ failed");\n        ESP_RETURN_ON_ERROR(esp_intr_free(bus->intr),TAG,"free IO IRQ failed");\n        bus->intr = NULL;\n    }\n    // remove from device list')
    # Task admission failure retains the queued transaction and its buffers;
    # it is never reported as successful scan submission.
    source=once(source,'    esp_intr_enable(bus->intr);\n    return ESP_OK;',
        '    ESP_RETURN_ON_ERROR(esp_intr_enable(bus->intr), TAG, "start pixel IRQ failed");\n    return ESP_OK;')
    return source

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('output',type=Path)
    args=ap.parse_args();original=args.source.read_text();args.output.write_text(prepare(original))
    print('Staged provider-private i80 source sha256='+hashlib.sha256(args.source.read_bytes()).hexdigest())
