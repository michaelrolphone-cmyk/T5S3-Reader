#pragma once
#include "PackageIdentity.h"

namespace RuntimePackages {
// Shared by capability discovery, provider selection and public enumeration.
// Host-copy files cannot become providers or consume the ordinary-entry cap.
// Total cursor work remains bounded: 64 ordinary + 64 copy + one Finder item.
class InstalledProviderRootScan {
 public:
  enum class Entry { Ordinary, CopyMetadata, Exhausted };
  static constexpr size_t kOrdinaryLimit = 64;
  Entry observe(const char* name, bool directory) {
    if (!directory && !std::strcmp(name, ".DS_Store")) {
      if (finder_) return Entry::Exhausted;
      finder_ = true;
      return Entry::CopyMetadata;
    }
    if (!directory && name[0] == '.' && name[1] == '_' && safeId(name + 2))
      return ++copies_ <= kOrdinaryLimit ? Entry::CopyMetadata : Entry::Exhausted;
    return ++ordinary_ <= kOrdinaryLimit ? Entry::Ordinary : Entry::Exhausted;
  }
 private:
  size_t ordinary_ = 0, copies_ = 0;
  bool finder_ = false;
};
} // namespace RuntimePackages
