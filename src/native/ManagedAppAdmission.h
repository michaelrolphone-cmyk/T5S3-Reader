#pragma once
#include <memory>
#include <string>

#include "runtime/packages/PackageIdentity.h"

namespace RuntimePackages {
// One private native-app invocation, owned by NativeAppHost. This captures the
// exact declared sidecar consumed by existing dependency/capability code and
// binds the existing ELF loader's owned bytes to the same manifest generation.
bool beginManagedAppAdmission(const Identity& identity, const char* sdPath);
void endManagedAppAdmission();
enum class ManagedAppMetadata { Unmanaged, Denied, Captured };
ManagedAppMetadata captureManagedAppSidecar(const char* sdPath, std::shared_ptr<const std::string>& sidecar,
                                            Identity* identity = nullptr);
}  // namespace RuntimePackages
