#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace FontCatalogValidation {

inline std::string identity(const std::string& name) {
  std::string result;
  result.reserve(name.size());
  for (unsigned char c : name) {
    result.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c));
  }
  return result;
}

template <typename Family>
bool canPublish(bool familiesFieldIsArray, const std::vector<Family>& candidate) {
  if (!familiesFieldIsArray) return false;

  std::unordered_set<std::string> names;
  for (const auto& family : candidate) {
    if (family.files.empty() || !names.insert(identity(family.name)).second) return false;
  }
  return true;
}

template <typename Family>
bool publish(std::vector<Family>& live, std::vector<Family>& candidate,
             bool familiesFieldIsArray) {
  if (!canPublish(familiesFieldIsArray, candidate)) return false;
  live.swap(candidate);
  return true;
}

}  // namespace FontCatalogValidation
