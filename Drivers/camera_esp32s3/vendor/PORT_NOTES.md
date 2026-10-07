# Retained sensor source

Source: https://github.com/espressif/esp32-camera/tree/v2.0.4
commit `e689c3b082985ee7b90198be32d330ce51ac5367`.

ov3660.c, ov3660.h, ov3660_regs.h, ov3660_settings.h and sensor.h are retained
from that source. resolution.c is the resolution table excerpt from driver/sensor.c.
Root Apache-2.0 LICENSE is retained. OpenMV-origin sensor headers/source retain
their original Ibrahim Abdelkader copyright and MIT licensing statements.

Port changes: remove DRAM_ATTR from the immutable OV3660 register tables because
independent ELF loading maps .rodata, not .dram1.* firmware linker sections.
Private sccb.h declares reads as int so failed reads propagate -1 instead of being
silently converted to 255. Private xclk.h declares the driver-owned LCD_CAM clock
setter without dragging firmware esp_camera.h into the module. Sensor code is
linked inside the ELF, never imported from firmware.
