# Xteink X4 Pro drivers

Board packages for the Xteink X4 Pro. They publish the existing RiscRTE capability ABI (`risc_driver_v2`, export `t5_driver_get` only) and do not reuse the T5S3 panel, touch axis, or I2C pin ownership.

Pin and protocol facts are derived from CrossPoint/FreeInk app1 (`ESP32S3_X4_TL_SSD1677`). They have not been run on an X4 Pro from this tree.

Load order: `platform-clock-v1`, `x4pro-i2c`, `x4pro-panel`, `x4pro-gt911`, `x4pro-buttons`, `x4pro-frontlight`, `x4pro-battery`, `x4pro-sd`.

## Boot cut

`env:xteink-x4-pro` skips the T5S3 bus, card, expander, charger, and i80 panel before any of them run. `x4BootToMainScreen()` uses the same SSD1677 sequence as `x4pro-panel` and draws the temporary main menu. HomeActivity is not on this path yet: it still targets the 960×540 renderer. This image has not been flashed.
