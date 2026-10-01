#include "WifiCredentialStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>

#include <utility>

// Initialize the static instance
WifiCredentialStore WifiCredentialStore::instance;

namespace {
// File format version (for binary migration)
constexpr uint8_t WIFI_FILE_VERSION = 2;

// File paths
constexpr char WIFI_FILE_BIN[] = "/.crosspoint/wifi.bin";
constexpr char WIFI_FILE_JSON[] = "/.crosspoint/wifi.json";
constexpr char WIFI_FILE_TMP[] = "/.crosspoint/wifi.json.tmp";
constexpr char WIFI_FILE_BAK_JSON[] = "/.crosspoint/wifi.json.bak";
constexpr char WIFI_FILE_BAK[] = "/.crosspoint/wifi.bin.bak";

// Legacy obfuscation key - "CrossPoint" in ASCII (only used for binary migration)
constexpr uint8_t LEGACY_OBFUSCATION_KEY[] = {0x43, 0x72, 0x6F, 0x73, 0x73, 0x50, 0x6F, 0x69, 0x6E, 0x74};
constexpr size_t LEGACY_KEY_LENGTH = sizeof(LEGACY_OBFUSCATION_KEY);

void legacyDeobfuscate(std::string& data) {
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= LEGACY_OBFUSCATION_KEY[i % LEGACY_KEY_LENGTH];
  }
}
}  // namespace

bool WifiCredentialStore::saveToFile() const {
  return saveSnapshot(credentials, lastConnectedSsid);
}

bool WifiCredentialStore::saveSnapshot(const std::vector<WifiCredential>& candidate,
                                       const std::string& lastConnected) const {
  Storage.mkdir("/.crosspoint");
  if (!JsonSettingsIO::saveWifiSnapshot(candidate, lastConnected, WIFI_FILE_TMP)) return false;
  bool hadLive = Storage.exists(WIFI_FILE_JSON);
  if (!hadLive && Storage.exists(WIFI_FILE_BAK_JSON)) {
    if (!Storage.rename(WIFI_FILE_BAK_JSON, WIFI_FILE_JSON)) {
      Storage.remove(WIFI_FILE_TMP);
      return false;
    }
    hadLive = true;
  }
  if (hadLive && Storage.exists(WIFI_FILE_BAK_JSON) && !Storage.remove(WIFI_FILE_BAK_JSON)) {
    Storage.remove(WIFI_FILE_TMP);
    return false;
  }
  if (hadLive && !Storage.rename(WIFI_FILE_JSON, WIFI_FILE_BAK_JSON)) {
    Storage.remove(WIFI_FILE_TMP);
    return false;
  }
  if (Storage.rename(WIFI_FILE_TMP, WIFI_FILE_JSON)) {
    if (hadLive) Storage.remove(WIFI_FILE_BAK_JSON);
    return true;
  }
  if (hadLive) Storage.rename(WIFI_FILE_BAK_JSON, WIFI_FILE_JSON);
  Storage.remove(WIFI_FILE_TMP);
  return false;
}

bool WifiCredentialStore::loadFromFile() {
  // Try JSON first
  if (Storage.exists(WIFI_FILE_JSON)) {
    String json = Storage.readFile(WIFI_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadWifi(*this, json.c_str(), &resave);
      if (result && resave) {
        LOG_DBG("WCS", "Resaving JSON with obfuscated passwords");
        saveToFile();
      }
      if (result) return true;
    }
  }

  // Recover the previous complete snapshot if power loss interrupted the
  // temp/rename transaction. The live path always wins when it validates.
  if (Storage.exists(WIFI_FILE_BAK_JSON)) {
    String json = Storage.readFile(WIFI_FILE_BAK_JSON);
    if (!json.isEmpty() && JsonSettingsIO::loadWifi(*this, json.c_str(), nullptr)) {
      if (!Storage.exists(WIFI_FILE_JSON)) Storage.rename(WIFI_FILE_BAK_JSON, WIFI_FILE_JSON);
      return true;
    }
  }

  // Fall back to binary migration
  if (Storage.exists(WIFI_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      if (saveToFile()) {
        Storage.rename(WIFI_FILE_BIN, WIFI_FILE_BAK);
        LOG_DBG("WCS", "Migrated wifi.bin to wifi.json");
        return true;
      } else {
        LOG_ERR("WCS", "Failed to save wifi during migration");
        return false;
      }
    }
  }

  credentials.clear();
  lastConnectedSsid.clear();
  return false;
}

bool WifiCredentialStore::loadFromBinaryFile() {
  FsFile file;
  if (!Storage.openFileForRead("WCS", WIFI_FILE_BIN, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version > WIFI_FILE_VERSION) {
    LOG_DBG("WCS", "Unknown file version: %u", version);
    return false;
  }

  if (version >= 2) {
    serialization::readString(file, lastConnectedSsid);
  } else {
    lastConnectedSsid.clear();
  }

  uint8_t count;
  serialization::readPod(file, count);

  credentials.clear();
  for (uint8_t i = 0; i < count && i < MAX_NETWORKS; i++) {
    WifiCredential cred;
    serialization::readString(file, cred.ssid);
    serialization::readString(file, cred.password);
    legacyDeobfuscate(cred.password);
    credentials.push_back(cred);
  }

  // LOG_DBG("WCS", "Loaded %zu WiFi credentials from binary file", credentials.size());
  return true;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64) return false;
  auto candidate = credentials;
  // Check if this SSID already exists and update it
  const auto cred = std::find_if(candidate.begin(), candidate.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != candidate.end()) {
    cred->password = password;
    LOG_DBG("WCS", "Updated credentials for: %s", ssid.c_str());
    if (!saveSnapshot(candidate, lastConnectedSsid)) return false;
    credentials = std::move(candidate);
    return true;
  }

  // Check if we've reached the limit
  if (candidate.size() >= MAX_NETWORKS) {
    LOG_DBG("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
    return false;
  }

  // Add new credential
  candidate.push_back({ssid, password});
  LOG_DBG("WCS", "Added credentials for: %s", ssid.c_str());
  if (!saveSnapshot(candidate, lastConnectedSsid)) return false;
  credentials = std::move(candidate);
  return true;
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  auto candidate = credentials;
  std::string candidateLast = lastConnectedSsid;
  const auto cred = std::find_if(candidate.begin(), candidate.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != candidate.end()) {
    candidate.erase(cred);
    LOG_DBG("WCS", "Removed credentials for: %s", ssid.c_str());
    if (ssid == candidateLast) candidateLast.clear();
    if (!saveSnapshot(candidate, candidateLast)) return false;
    credentials = std::move(candidate);
    lastConnectedSsid = std::move(candidateLast);
    return true;
  }
  return false;  // Not found
}

const WifiCredential* WifiCredentialStore::findCredential(const std::string& ssid) const {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });

  if (cred != credentials.end()) {
    return &*cred;
  }

  return nullptr;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const { return findCredential(ssid) != nullptr; }

bool WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) {
  if (lastConnectedSsid != ssid) {
    if (ssid.size() > 32 || !saveSnapshot(credentials, ssid)) return false;
    lastConnectedSsid = ssid;
  }
  return true;
}

const std::string& WifiCredentialStore::getLastConnectedSsid() const { return lastConnectedSsid; }

bool WifiCredentialStore::clearLastConnectedSsid() {
  if (!lastConnectedSsid.empty()) {
    if (!saveSnapshot(credentials, "")) return false;
    lastConnectedSsid.clear();
  }
  return true;
}

bool WifiCredentialStore::clearAll() {
  if (!saveSnapshot({}, "")) return false;
  credentials.clear();
  lastConnectedSsid.clear();
  LOG_DBG("WCS", "Cleared all WiFi credentials");
  return true;
}
