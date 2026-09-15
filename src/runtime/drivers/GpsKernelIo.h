#pragma once
#include <T5DriverApi.h>
bool gpsKernelAvailable();
const t5_kernel_io_v1* gpsKernelClaim();
void gpsKernelRelease();
