#pragma once
#include <RiscProviderV2.h>
#define RISC_CAMERA_ESP32S3_PROFILE "board.camera.esp32s3.profile"
/* Installed wiring data, not authority. Trusted deployment must reserve these
 * pins, LCD_CAM and the selected GDMA RX channel exclusively before activation.
 * No bus-controller claim: SCCB is private bit-banged GPIO inside camera ELF. */
typedef struct {
    uint32_t api_version, struct_size;
    uint8_t data[8], pclk, vsync, href, xclk, sda, scl, dma_channel, address;
    uint32_t xclk_hz, sensor_pid;
} risc_camera_esp32s3_profile_v1;
