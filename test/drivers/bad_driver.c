#include "T5DriverApi.h"
static const t5_driver_v1 bad = {.abi_version = 999, .struct_size = sizeof(t5_driver_v1)};
const t5_driver_v1 *t5_driver_get(uint32_t abi) { (void)abi; return &bad; }
