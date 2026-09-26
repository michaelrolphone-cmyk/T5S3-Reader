#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
bridge = (repo / "src/native/NativeNetworkBridge.cpp").read_text()
host = (repo / "src/native/NativeAppHost.cpp").read_text()
header = (repo / "src/native/NativeNetworkBridge.h").read_text()

assert "http.setCookieJar(&nativeCookieJar)" in bridge
assert "kMaxSessionCookies = 8" in bridge
assert "nativeNetworkBegin()" in bridge
assert "nativeNetworkEnd()" in bridge
assert "clearNativeCookieJar();" in bridge
assert "nativeNetworkBegin();" in host
assert "nativeNetworkEnd();" in host
assert host.index("nativeNetworkBegin();") < host.index("launch_elf_app(path)")
assert host.index("launch_elf_app(path)") < host.index("nativeNetworkEnd();")
assert "void nativeNetworkBegin();" in header
assert "void nativeNetworkEnd();" in header

print("Native HTTP cookie session lifecycle source invariants passed")
