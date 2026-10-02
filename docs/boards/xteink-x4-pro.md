# Xteink X4 Pro drivers

Board packages for the Xteink X4 Pro. They publish the existing RiscRTE capability ABI (`risc_driver_v2`, export `t5_driver_get` only) and do not reuse the T5S3 panel, touch axis, or I2C pin ownership.

Pin and protocol facts are derived from CrossPoint/FreeInk app1 (`ESP32S3_X4_TL_SSD1677`). They have not been run on an X4 Pro from this tree.

Load order: `platform-clock-v1`, `x4pro-i2c`, `x4pro-panel`, `x4pro-gt911`, `x4pro-buttons`, `x4pro-frontlight`, `x4pro-battery`, `x4pro-sd`.

`i2c-esp32s3-v2` stays the T5S3 bus. X4 Pro I2C is a separate provider on GPIO 39/38.
