#pragma once
#include "RiscCameraEsp32s3ProfileV1.h"
#include <stddef.h>
/* Internal ELF backend; never an imported firmware camera bridge. */
bool cam_hw_start(const risc_camera_esp32s3_profile_v1 *profile);
bool cam_hw_begin(unsigned quality);
/* AGAIN/OK/negative stream-compatible result. Output remains private. */
int32_t cam_hw_poll(const uint8_t **bytes, uint32_t *length);
bool cam_hw_stop_capture(void);
bool cam_hw_shutdown(void);
uint64_t cam_hw_now(void);
void cam_hw_yield(void);
