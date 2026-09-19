#include "runtime/packages/PackageMutationGate.h"

// A bridge-local atomic would pass a single-TU test; this independent TU
// verifies the ordinary package bridge and historical recovery share ONE flag.
extern "C" bool packageGateTryFromOtherTranslationUnit() {
    RuntimePackages::ScopedPackageMutation lock;
    return static_cast<bool>(lock);
}

extern "C" void* packageGateAddressFromOtherTranslationUnit() {
    return &RuntimePackages::packageMutationFlag();
}
