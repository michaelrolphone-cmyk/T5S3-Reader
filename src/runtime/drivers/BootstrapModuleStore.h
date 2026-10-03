#pragma once
// Read-only POSIX module-store bootstrap. Caller mounts a separately
// provisioned filesystem; this code never formats, installs or writes files.
namespace RuntimeInstalledProviders {
bool loadBootstrapPackages(const char* root, const char* expectedBoard);
}
