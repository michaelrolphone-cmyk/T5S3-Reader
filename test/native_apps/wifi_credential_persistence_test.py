#!/usr/bin/env python3
"""Source contract for Wi-Fi credential transaction and failure handling."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
store = (ROOT / "src/WifiCredentialStore.cpp").read_text()
codec = (ROOT / "src/JsonSettingsIO.cpp").read_text()
ui = (ROOT / "src/activities/network/WifiSelectionActivity.cpp").read_text()


def body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    return source[start : source.index(next_signature, start)]


add = body(store, "bool WifiCredentialStore::addCredential(", "bool WifiCredentialStore::removeCredential(")
remove = body(store, "bool WifiCredentialStore::removeCredential(", "const WifiCredential* WifiCredentialStore::findCredential(")
set_last = body(store, "bool WifiCredentialStore::setLastConnectedSsid(", "const std::string& WifiCredentialStore::getLastConnectedSsid(")
clear_all = store[store.index("bool WifiCredentialStore::clearAll("):]
load = body(codec, "bool JsonSettingsIO::loadWifi(", "// ---- RecentBooksStore ----")
save = body(store, "bool WifiCredentialStore::saveSnapshot(", "bool WifiCredentialStore::loadFromFile(")

# Candidate state is durably staged before the live store changes. These
# assertions fail on the prior implementation, which modified live vectors first.
for method in (add, remove):
    assert "saveSnapshot(" in method
    assert method.index("saveSnapshot(") < method.index("credentials = std::move(candidate)")
assert set_last.index("saveSnapshot(") < set_last.index("lastConnectedSsid = ssid")
assert clear_all.index("saveSnapshot(") < clear_all.index("credentials.clear()")

# Save failures happen on a temporary path. A complete prior file is moved
# aside, restored after failed promotion, and available after interrupted rename.
assert "WIFI_FILE_TMP" in save and "WIFI_FILE_BAK_JSON" in save
assert save.index("saveWifiSnapshot") < save.index("rename(WIFI_FILE_JSON, WIFI_FILE_BAK_JSON)")
assert "rename(WIFI_FILE_BAK_JSON, WIFI_FILE_JSON)" in save

# Malformed/schema-invalid JSON must fail before publishing any parsed fields.
assert 'deserializeJson(doc, json)' in load
assert 'is<JsonArrayConst>()' in load
assert 'obj["ssid"].is<const char*>()' in load
assert load.index("store.lastConnectedSsid = lastConnectedSsid") > load.index("for (JsonObject obj : arr)")
assert load.index("store.credentials = std::move(credentials)") > load.index("for (JsonObject obj : arr)")

# Both input methods surface write errors and leave the explicit prompt active.
for prompt in ("SAVE_PROMPT", "FORGET_PROMPT"):
    section = body(ui, f"if (state == WifiSelectionState::{prompt})", "if (state == WifiSelectionState::" + ("FORGET_PROMPT" if prompt == "SAVE_PROMPT" else "CONNECTED") + ")")
    assert "Could not save Wi-Fi credentials" in section or "Could not forget Wi-Fi network" in section
assert '"Could not save Wi-Fi credentials"' in ui
assert '"Could not forget Wi-Fi network"' in ui
print("Wi-Fi credential persistence source contract PASS")
