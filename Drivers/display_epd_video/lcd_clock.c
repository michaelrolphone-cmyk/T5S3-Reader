/* Private LCD clock only. GDMA/system clocks belong to the resident CPU port. */
#include "freertos/FreeRTOS.h"
#include "hal/clk_gate_ll.h"
#include "driver/periph_ctrl.h"
#include <assert.h>
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned refs;
void periph_module_enable(periph_module_t module) {
    assert(module==PERIPH_LCD_CAM_MODULE);
    portENTER_CRITICAL_SAFE(&lock);
    if(refs++==0)periph_ll_enable_clk_clear_rst(PERIPH_LCD_CAM_MODULE);
    portEXIT_CRITICAL_SAFE(&lock);
}
void periph_module_disable(periph_module_t module) {
    assert(module==PERIPH_LCD_CAM_MODULE);
    portENTER_CRITICAL_SAFE(&lock);
    assert(refs);
    if(--refs==0)periph_ll_disable_clk_set_rst(PERIPH_LCD_CAM_MODULE);
    portEXIT_CRITICAL_SAFE(&lock);
}
void periph_module_reset(periph_module_t module) {
    assert(module==PERIPH_LCD_CAM_MODULE);
    portENTER_CRITICAL_SAFE(&lock);
    periph_ll_reset(PERIPH_LCD_CAM_MODULE);
    portEXIT_CRITICAL_SAFE(&lock);
}
