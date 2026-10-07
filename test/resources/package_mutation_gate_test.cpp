#include "runtime/packages/PackageMutationGate.h"

#include <cassert>
#include <cstdio>

extern "C" bool packageGateTryFromOtherTranslationUnit();
extern "C" void* packageGateAddressFromOtherTranslationUnit();

int main() {
    assert(&RuntimePackages::packageMutationFlag() ==
           packageGateAddressFromOtherTranslationUnit());
    assert(packageGateTryFromOtherTranslationUnit());
    {
        RuntimePackages::ScopedPackageMutation first;
        assert(first);
        assert(!packageGateTryFromOtherTranslationUnit());
        RuntimePackages::ScopedPackageMutation denied;
        assert(!denied);
        // A failed guard must NOT clear the flag held by another transaction.
        assert(!packageGateTryFromOtherTranslationUnit());
    }
    assert(packageGateTryFromOtherTranslationUnit());
    std::puts("PASS: ordinary package and legacy recovery share one retryable mutation gate");
}
