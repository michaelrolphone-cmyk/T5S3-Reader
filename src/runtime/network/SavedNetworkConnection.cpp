#include "SavedNetworkConnection.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include <vector>

#include "NetworkService.h"
#include "WifiCredentialStore.h"

namespace RuntimeNetwork {

bool ensureSavedConnection(uint32_t timeoutMs) {
  if (ready()) return true;

  WIFI_STORE.loadFromFile();
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  const auto& credentials = WIFI_STORE.getCredentials();
  const WifiCredential* preferred = last.empty() ? nullptr : WIFI_STORE.findCredential(last);
  if (preferred && preferred->ssid.empty()) preferred = nullptr;

  // The store is bounded to a small number of profiles. Keep an ordered,
  // de-duplicated pointer list (preferred first) without copying credentials.
  std::vector<const WifiCredential*> candidates;
  candidates.reserve(credentials.size());
  if (preferred) candidates.push_back(preferred);
  for (const auto& credential : credentials) {
    if (credential.ssid.empty()) continue;
    if (preferred && credential.ssid == preferred->ssid) continue;
    bool duplicate = false;
    for (const auto* candidate : candidates) {
      if (candidate->ssid == credential.ssid) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) candidates.push_back(&credential);
  }
  if (candidates.empty()) return false;

  const uint32_t started = ::millis();
  const uint32_t budget = timeoutMs ? timeoutMs : 15000u;
  constexpr uint32_t kPollMs = 25;
  for (size_t index = 0; index < candidates.size(); ++index) {
    const uint32_t elapsed = ::millis() - started;
    if (elapsed >= budget) break;

    const uint32_t remaining = budget - elapsed;
    const uint32_t attemptsLeft = static_cast<uint32_t>(candidates.size() - index);
    uint32_t attemptBudget = remaining / attemptsLeft;
    if (attemptBudget == 0) attemptBudget = 1;
    const uint32_t attemptDeadline = elapsed + attemptBudget;

    const WifiCredential* credential = candidates[index];
    wifi().connect(credential->ssid.c_str(),
                   credential->password.empty() ? nullptr : credential->password.c_str());

    while (true) {
      if (ready()) {
        WIFI_STORE.setLastConnectedSsid(credential->ssid);
        return true;
      }

      const uint32_t nowElapsed = ::millis() - started;
      if (nowElapsed >= attemptDeadline || nowElapsed >= budget) break;

      const ConnectionState current = state().connection;
      if (current == ConnectionState::Failed || current == ConnectionState::NetworkNotFound) break;

      uint32_t waitMs = attemptDeadline - nowElapsed;
      if (waitMs > kPollMs) waitMs = kPollMs;
      esp_task_wdt_reset();
      delay(waitMs);
    }

    if (ready()) {
      WIFI_STORE.setLastConnectedSsid(credential->ssid);
      return true;
    }
  }
  return false;
}

}  // namespace RuntimeNetwork
