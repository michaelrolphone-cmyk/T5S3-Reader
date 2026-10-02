# Xteink X4 Pro drivers

Board packages for the Xteink X4 Pro. They publish the existing RiscRTE capability ABI (`risc_driver_v2`, export `t5_driver_get` only) and do not reuse the T5S3 panel, touch axis, or I2C pin ownership.

Pin and protocol facts are derived from CrossPoint/FreeInk app1 (`ESP32S3_X4_TL_SSD1677`). They have not been run on an X4 Pro from this tree.

Load order: `platform-clock-v1`, `x4pro-i2c`, `x4pro-panel`, `x4pro-gt911`, `x4pro-buttons`, `x4pro-frontlight`, `x4pro-battery`, `x4pro-sd`.

## First diagnostic boot

`env:xteink-x4-pro` does not call `HalSystem::begin()`, so the desk-clock timer wake cannot start the legacy display. It loads embedded `platform-clock-v1`, `x4pro-panel`, `x4pro-buttons`, and `x4pro-frontlight` through `ProviderModuleV2::loadVerifiedBytes`. The first paint uses `display.output` only. Buttons are consumed through `input.navigation`. Power is mapped to Confirm. Frontlight stays off.

This artifact is SSD1677-specific. UC8179 and UC8279 are unresolved. GPIO1 is the recovered peripheral-enable name and is not driven. Touch power and SD power are not activated.

Partition layout in this tree is app0 at `0x10000` size `0x640000` and app1 at `0x650000`. Stock CrossPoint layout compatibility is unverified. The intended artifact is the application image at `0x10000`, not a merged flash. No bootloader, partition table, OTA metadata, or NVS rewrite is part of this change.

Deferred: GT911 sequencing, charge-status polarity, SDMMC filesystem, I2C 39/38 after the high-bank GPIO fix, and sleep/wake policy.
