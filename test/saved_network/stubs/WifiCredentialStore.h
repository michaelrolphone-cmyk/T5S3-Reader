#pragma once

#include <string>
#include <utility>
#include <vector>

struct WifiCredential {
  std::string ssid;
  std::string password;
};

class WifiCredentialStore {
 public:
  static WifiCredentialStore& getInstance() {
    static WifiCredentialStore store;
    return store;
  }

  bool loadFromFile() { return true; }
  const std::string& getLastConnectedSsid() const { return last_connected; }
  const WifiCredential* findCredential(const std::string& ssid) const {
    for (const auto& credential : credentials) {
      if (credential.ssid == ssid) return &credential;
    }
    return nullptr;
  }
  const std::vector<WifiCredential>& getCredentials() const { return credentials; }
  void setLastConnectedSsid(const std::string& ssid) { last_connected = ssid; }
  void reset(std::initializer_list<std::pair<std::string, std::string>> entries, std::string last) {
    credentials.clear();
    for (const auto& entry : entries) credentials.push_back({entry.first, entry.second});
    last_connected = std::move(last);
  }

 private:
  std::vector<WifiCredential> credentials;
  std::string last_connected;
};

#define WIFI_STORE WifiCredentialStore::getInstance()
