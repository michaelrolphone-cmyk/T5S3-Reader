#pragma once
#include <string>
#include <vector>
struct WifiCredential;
class WifiCredentialStore;
namespace JsonSettingsIO {
bool saveWifi(const WifiCredentialStore&, const char*);
bool saveWifiSnapshot(const std::vector<WifiCredential>&, const std::string&, const char*);
bool loadWifi(WifiCredentialStore&, const char*, bool*);
}
