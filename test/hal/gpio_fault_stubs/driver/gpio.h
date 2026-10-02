#pragma once
#include <cstdint>
using esp_err_t=int;
using gpio_num_t=int;
struct gpio_config_t { uint64_t pin_bit_mask; };
