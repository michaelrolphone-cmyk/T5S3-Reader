#pragma once
// Validates the installed GPS manifest and bounded ELF SHA-256 before dlopen.
bool validateGpsDriverPackage();
constexpr const char* GPS_DRIVER_ELF = "/sd/Drivers/gps-nmea/driver.elf";
