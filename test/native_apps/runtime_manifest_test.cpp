#include "native/AppManifest.h"
#include <cassert>
#include <string>

static const std::string base = R"({"min_firmware_version":"1.2.48","version":"0.1.0","display_name":"Text Editor","file_name":"text_editor.elf","icon":"solid:f15c","size_bytes":15340,"sha256":"bb08e32710b605f8d207a5e8abfb7643075ba9c02b5178c069838d6676bb7f4b")";
static bool parse(const std::string& fields, RuntimeDevices::AppCapabilityRequirements* required = nullptr) {
  t5_app_manifest_t app{};
  return parseAppManifest(base + fields + "}", app, nullptr, false, required);
}
int main() {
  assert(parse(""));
  assert(parse(R"(,"optional":[])"));
  assert(parse(R"(,"requires":[])"));
  RuntimeDevices::AppCapabilityRequirements required{};
  assert(parse(R"(,"optional":[{"capability":"usb.hid.keyboard","api":">=1"}])", &required));
  assert(required.count == 0); // No provider is required for optional functionality.
  assert(parse(R"(,"requires":[{"capability":"serial.port","api":">=2"}],"optional":[{"capability":"usb.hid.keyboard","api":">=1"}])", &required));
  assert(required.count == 1 && required.entries[0].minApi == 2);
  assert(std::string(required.entries[0].capability) == "serial.port");
  assert(!parse(R"(,"optional":{})"));
  assert(!parse(R"(,"optional":["usb.hid.keyboard"])"));
  assert(!parse(R"(,"optional":[{"capability":"usb.hid.keyboard","api":">=0"}])"));
  assert(!parse(R"(,"optional":[{"capability":"usb.hid.keyboard","api":">=1","extra":true}])"));
  assert(!parse(R"(,"requires":[{"capability":"serial.port","api":">=1"}],"optional":[{"capability":"serial.port","api":">=1"}])"));
  assert(!parse(R"(,"optional":[{"capability":"serial.port","api":">=1"},{"capability":"serial.port","api":">=1"}])"));
  std::string list = ",\"optional\":[";
  for (unsigned i = 0; i < 7; ++i) {
    if (i) list += ',';
    list += "{\"capability\":\"test." + std::to_string(i) + "\",\"api\":\">=1\"}";
  }
  assert(!parse(list + "]"));
}
