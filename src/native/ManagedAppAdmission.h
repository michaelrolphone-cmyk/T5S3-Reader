#pragma once
#include <memory>
#include <string>

#include "AppIntegrity.h"
#include "runtime/packages/PackageIdentity.h"

namespace RuntimePackages {
// One private native-app invocation, owned by NativeAppHost. This captures the
// exact declared sidecar consumed by existing dependency/capability code and
// binds the existing ELF loader's owned bytes to the same manifest generation.
bool beginManagedAppAdmission(const Identity& identity, const char* sdPath);
using LegacyAppSidecarValidator = bool (*)(const std::string& json, const std::string& filename,
                                           AppIntegrity& integrity);
bool beginLooseAppAdmission(const char* sdPath, LegacyAppSidecarValidator validator);
void endManagedAppAdmission();
enum class ManagedAppMetadata { Unmanaged, Denied, Captured, CapturedLegacy };
ManagedAppMetadata captureManagedAppSidecar(const char* sdPath, std::shared_ptr<const std::string>& sidecar,
                                            Identity* identity = nullptr);
}  // namespace RuntimePackages
