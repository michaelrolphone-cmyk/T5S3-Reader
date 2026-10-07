#pragma once
/* Private transition for spi-esp32s3-v1 alone. Bounded raw controller access,
 * never SD commands/FAT semantics. Not an ordinary app or peripheral import.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool risc_fw_spi_begin_v1(uint8_t chip_select, uint32_t hz, bool selected);
bool risc_fw_spi_select_v1(bool selected);
bool risc_fw_spi_transfer_v1(const uint8_t *tx, uint8_t *rx, size_t bytes);
bool risc_fw_spi_end_v1(void);
#ifdef __cplusplus
}
#endif
