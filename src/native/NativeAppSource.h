#pragma once
#include "../../lib/NativeApps/include/NativeAppSource.h"
#include <StorageSource.h>
namespace RuntimePackages {
// Copy the exact source proof into admission. No pointer into a Host stack is
// retained. An empty proof denotes the unchanged legacy backend only.
bool captureNativeAppSource(const char* path, StorageSourceLease& proof);
bool matchesNativeAppSource(const char* path, const StorageSourceLease& proof);
}
