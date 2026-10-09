#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace RuntimeInstalledProviders {
using BootPackageReader = bool (*)(const std::string&, size_t, std::vector<uint8_t>&);
// Read the SD board selection and ordinary /Drivers generations. Admission
// copies bytes, pins package roots and never starts hardware. The caller must
// release boot media before finishBootstrapHandoff permits graph acquisition.
bool loadBootstrapPackages(const char* root, const char* expectedBoard,
                           BootPackageReader reader);
bool finishBootstrapHandoff();
}
