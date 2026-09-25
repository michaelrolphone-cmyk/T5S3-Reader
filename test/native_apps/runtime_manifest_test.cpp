#include "native/AppManifest.h"
#include <cassert>
#include <string>

static const std::string base = R"({"min_firmware_version":"1.2.48","version":"0.1.0","display_name":"Text Editor","file_name":"text_editor.elf","icon":"solid:f15c","size_bytes":15340,"sha256":"bb08e32710b605f8d207a5e8abfb7643075ba9c02b5178c069838d6676bb7f4b")";
static bool parse(const std::string& fields,
                  RuntimeDevices::AppCapabilityRequirements* required = nullptr,
                  AppFileTypes* fileTypes = nullptr) {
  t5_app_manifest_t app{};
  return parseAppManifest(base + fields + "}", app, nullptr, false, required, fileTypes);
}
int main() {
  assert(parse(""));
  assert(parse(R"(,"optional":[])"));
  assert(parse(R"(,"requires":[])"));
  AppFileTypes fileTypes{};
  assert(parse(R"(,"supported_file_types":[".txt",".md"])", nullptr, &fileTypes));
  assert(fileTypes.count == 2);
  assert(std::string(fileTypes.values[0]) == ".txt");
  assert(std::string(fileTypes.values[1]) == ".md");
  assert(!parse(R"(,"supported_file_types":["txt"])"));
  assert(!parse(R"(,"supported_file_types":[".TXT"])"));
  assert(!parse(R"(,"supported_file_types":[".txt",".txt"])"));
  assert(!parse(R"(,"supported_file_types":{})"));
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
