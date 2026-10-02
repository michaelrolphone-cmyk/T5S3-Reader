#pragma once

#include <atomic>

// Normal package transactions and the Driver Manager's historical-stage
// recovery both mutate /Drivers. They must participate in the SAME gate;
// separate bridge-local locks permit concurrent rename/remove/rollback of a
// live ordinary transaction. This gate represents transaction serialization,
// not permission to operate on a package or a substitute for PackageUseGate.
namespace RuntimePackages {

inline std::atomic_flag& packageMutationFlag() {
    static std::atomic_flag gate = ATOMIC_FLAG_INIT;
    return gate;
}

class ScopedPackageMutation final {
public:
    ScopedPackageMutation()
        : acquired_(!packageMutationFlag().test_and_set(std::memory_order_acquire)) {}
    ~ScopedPackageMutation() {
        if (acquired_) packageMutationFlag().clear(std::memory_order_release);
    }
    ScopedPackageMutation(const ScopedPackageMutation&) = delete;
    ScopedPackageMutation& operator=(const ScopedPackageMutation&) = delete;
    explicit operator bool() const { return acquired_; }

private:
    bool acquired_ = false;
};

} // namespace RuntimePackages
