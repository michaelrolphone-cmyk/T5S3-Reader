#pragma once
constexpr int PCA9535_IO00_LORA_GPS_EN = 0;
namespace BoardT5S3 {
bool writePca9535Pin(int, bool);
bool setPca9535PinMode(int, int);
}
