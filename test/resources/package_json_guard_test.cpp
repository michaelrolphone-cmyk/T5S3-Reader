#include "runtime/packages/PackageJsonGuard.h"

#include <cassert>
#include <cstdio>
#include <string>

using RuntimePackages::safePackageJsonObject;

namespace {
bool accepts(const std::string& json) {
  return safePackageJsonObject(json.data(), json.size());
}

void valid() {
  assert(accepts(R"({"type":"driver","id":"gps-nmea","version":"1.0.0","requires":[{"capability":"kernel.serial","api":1},{"capability":"kernel.clock","api":2}]})"));
  assert(accepts(R"( {"a":{},"b":[true,false,null,0,-2.5e+4,"escaped \"quote\" and \\slash", "\u00e9", "\ud83d\ude00"],"c":{"a":1}} )"));
  // A nested object may use a key also present in an outer object.
  assert(accepts(R"({"id":"outer","requires":[{"id":"inner"}]})"));
  assert(accepts("{}"));
}

void duplicates() {
  assert(!accepts(R"({"id":"first","id":"second"})"));
  assert(!accepts(R"({"requires":[{"capability":"one","capability":"two"}]})"));
  assert(!accepts(R"({"a":{"x":1,"x":2}})"));
  // Escaped key spellings cannot alias a regular key after JSON decoding.
  assert(!accepts(R"({"id":"one","\u0069d":"two"})"));
  assert(!accepts(R"({"i\u0064":"one"})"));
}

void malformed() {
  const char* bad[] = {
    "", "null", "[]", R"({"a":1}garbage)", R"({"a":1,})", R"({"a" 1})",
    R"({"a":[1,]})", R"({"a":01})", R"({"a":+1})", R"({"a":-})",
    R"({"a":1.})", R"({"a":1e})", R"({"a":truth})", R"({"a":"\q"})",
    R"({"a":"\u12xz"})", R"({"a":"\ud800"})", R"({"a":"\udc00"})",
    R"({"a":"\ud800\u0061"})", R"({"a":"unfinished})", "{\"a\":\"bad\nvalue\"}"
  };
  for (const char* input : bad) assert(!accepts(input));
  std::string nul = "{\"id\":1}";
  nul.insert(3, 1, '\0');
  assert(!accepts(nul));
  assert(!safePackageJsonObject(nullptr, 0));
}

void bounds() {
  std::string large = "{\"a\":\"" + std::string(4096, 'x') + "\"}";
  assert(!accepts(large));
  std::string longKey = "{\"" + std::string(64, 'a') + "\":1}";
  assert(!accepts(longKey));
  std::string many = "{";
  for (int i = 0; i < 65; ++i) {
    if (i) many += ',';
    many += "\"k" + std::to_string(i) + "\":0";
  }
  many += "}";
  assert(!accepts(many));
  std::string deep = "{";
  for (int i = 0; i < 11; ++i) deep += "\"a\":{";
  deep += "}";
  for (int i = 0; i < 11; ++i) deep += "}";
  assert(!accepts(deep));
  std::string array = "{\"a\":[";
  for (int i = 0; i < 129; ++i) { if (i) array += ','; array += '0'; }
  array += "]}";
  assert(!accepts(array));
}
} // namespace

int main() {
  valid();
  duplicates();
  malformed();
  bounds();
  std::puts("Package JSON guard: duplicate keys, aliases, malformed tokens and bounds passed");
}
