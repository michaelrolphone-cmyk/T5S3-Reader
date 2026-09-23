#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct DriverPackageInfo {
    char id[64] = {};
    char version[32] = {};
    char capability[64] = {};
    uint32_t sizeBytes = 0;
};

// Parses and validates the architecture/ABI/package contract without opening the ELF.
bool parseDriverPackageManifest(const std::string& json, DriverPackageInfo& out);

// Validates manifest contract, ELF structure, declared size, and SHA-256.
bool validateDriverPayload(const std::string& manifestJson, const char* elfVfsPath,
                           DriverPackageInfo* out = nullptr, bool verifyContents = true);

// Returns the installed manifest version for a managed driver package.
bool getInstalledDriverVersion(const char* id, char* version, size_t capacity);

// Atomically installs or upgrades a validated staged ELF. Installation never binds or activates hardware.
bool installStagedDriverPackage(const std::string& manifestJson, const char* stagedElfVfsPath);

// Validates the installed GPS package and its stricter first-party capability contract before dlopen.
bool validateGpsDriverPackage();

constexpr const char* GPS_DRIVER_ELF = "/sd/Drivers/gps-nmea/driver.elf";
