/* Provider-owned SoC pins for the T5 8-bit raw EPD only. GPIO46 belongs to
 * LoRa and is intentionally absent. No firmware GPIO driver import/fallback. */
#include "driver/gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_periph.h"
#include "soc/gpio_sig_map.h"
#include "esp_rom_gpio.h"
#include "esp_attr.h"
static const uint64_t pins=(1ULL<<4)|(1ULL<<5)|(1ULL<<6)|(1ULL<<7)|(1ULL<<8)|
 (1ULL<<15)|(1ULL<<16)|(1ULL<<17)|(1ULL<<18)|(1ULL<<41)|(1ULL<<42)|(1ULL<<45)|(1ULL<<48);
static bool owned(gpio_num_t pin) { return pin>=0 && pin<49 && (pins&(1ULL<<pin)); }
esp_err_t IRAM_ATTR gpio_set_level(gpio_num_t pin,uint32_t level) {
 if(!owned(pin) || level>1)return ESP_ERR_INVALID_ARG;
 gpio_ll_set_level(&GPIO,pin,level);return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t pin,gpio_mode_t mode) {
 if(!owned(pin) || (mode!=GPIO_MODE_OUTPUT && mode!=GPIO_MODE_DISABLE))return ESP_ERR_INVALID_ARG;
 esp_rom_gpio_pad_select_gpio(pin);esp_rom_gpio_pad_unhold(pin);
 gpio_ll_intr_disable(&GPIO,pin);gpio_ll_input_disable(&GPIO,pin);
 gpio_ll_pullup_dis(&GPIO,pin);gpio_ll_pulldown_dis(&GPIO,pin);
 gpio_ll_od_disable(&GPIO,pin);
 gpio_ll_iomux_func_sel(GPIO_PIN_MUX_REG[pin],PIN_FUNC_GPIO);
 esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);
 if(mode==GPIO_MODE_OUTPUT)gpio_ll_output_enable(&GPIO,pin);
 else gpio_ll_output_disable(&GPIO,pin);
 return ESP_OK;
}
esp_err_t gpio_reset_pin(gpio_num_t pin) {
 return gpio_set_direction(pin,GPIO_MODE_DISABLE);
}
esp_err_t gpio_config(const gpio_config_t *config) {
 if(!config || !config->pin_bit_mask || (config->pin_bit_mask&~pins) ||
    config->mode!=GPIO_MODE_OUTPUT || config->pull_up_en!=GPIO_PULLUP_DISABLE ||
    config->pull_down_en!=GPIO_PULLDOWN_DISABLE || config->intr_type!=GPIO_INTR_DISABLE)
   return ESP_ERR_INVALID_ARG;
 for(unsigned pin=0;pin<49;++pin)if(config->pin_bit_mask&(1ULL<<pin)) {
  esp_err_t result=gpio_set_direction((gpio_num_t)pin,GPIO_MODE_OUTPUT);
  if(result!=ESP_OK)return result;
 }
 return ESP_OK;
}
