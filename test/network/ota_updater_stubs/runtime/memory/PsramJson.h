#pragma once
#include <cstddef>
namespace RuntimeMemory {
class PsramJsonAllocator {};
class PsramGrowingTextStream {
 public:
  bool good() const { return true; }
  bool empty() const { return false; }
  const char* chars() const { return "{}"; }
  size_t size() const { return 2; }
};
}
