/* Exact SoC pad map used by the shared USB-only phy_gpio.c. Its admission
 * rejects every pin except DM19/DP20; no firmware GPIO driver/table is imported. */
#include <stdint.h>
#include <soc/soc_caps.h>
#include <soc/io_mux_reg.h>
const uint32_t GPIO_PIN_MUX_REG[SOC_GPIO_PIN_COUNT]={
 [19]=IO_MUX_GPIO19_REG,[20]=IO_MUX_GPIO20_REG
};
