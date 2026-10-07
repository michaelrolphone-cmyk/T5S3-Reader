#!/usr/bin/env python3
"""Keep OTA's connected-without-DHCP path separate from saved fallback."""

from pathlib import Path


source = Path(__file__).resolve().parents[2] / "src/native/NativeOtaBridge.cpp"
text = source.read_text(encoding="utf-8")
start = text.index("bool ensureOtaNetworkReady()")
body_start = text.index("{", start)
depth = 0
end = None
for offset in range(body_start, len(text)):
    if text[offset] == "{":
        depth += 1
    elif text[offset] == "}":
        depth -= 1
        if depth == 0:
            end = offset + 1
            break
assert end is not None
body = text[body_start:end]

assert "if (RuntimeNetwork::ready()) return true;" in body
assert "state.connection == RuntimeNetwork::ConnectionState::Connected" in body
assert "Selected Wi-Fi did not obtain an IP address" in body
assert "RuntimeNetwork::ensureSavedConnection(kWifiConnectTimeoutMs)" in body
assert body.index("Selected Wi-Fi did not obtain an IP address") < body.index(
    "RuntimeNetwork::ensureSavedConnection(kWifiConnectTimeoutMs)"
)
assert "RuntimeNetwork::wifi().connect" not in body
assert "WIFI_STORE" not in body
print("OTA network fallback contract passed")
