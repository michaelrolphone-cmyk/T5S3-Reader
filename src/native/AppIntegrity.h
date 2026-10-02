#pragma once
#include <cstdint>
// Optional legacy sidecar content declaration. This is not an identity/grant.
struct AppIntegrity {
  bool present = false;
  uint32_t sizeBytes = 0;
  char sha256[65]{};
};
