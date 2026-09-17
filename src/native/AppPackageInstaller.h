#pragma once

// Transitional adapter for the existing /Apps/<name>.elf and .json layout.
// Only a validated ELF basename is accepted; paths are runtime-constructed.
// These functions do not authenticate publisher signatures or grant rights.
namespace RuntimePackages {

// Validates the manifest's filename and the ELF header/size. If the manifest
// declares a digest, hashes the actual ELF. A new staged release MUST declare
// both size_bytes and sha256; old installed sidecars may omit both solely for
// migration compatibility and are never represented as authenticated.
bool verifyAppPair(const char* elfPath, const char* manifestPath,
                   const char* expectedFilename, bool requireDigest);

// Recover interrupted publish/rollback before reading, launching or updating
// an installed app. A corrupt or ambiguous state is left in place, not erased.
bool recoverAppPair(const char* filename);

// Delete only the two known disposable .part paths after recovery succeeds.
bool clearAppStage(const char* filename);

// Publish the already-downloaded and verified .part pair. The caller must
// independently prohibit replacing a mapped/running application.
bool publishAppPair(const char* filename, bool replacementAllowed);

} // namespace RuntimePackages
