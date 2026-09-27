#!/usr/bin/env python3
"""Lock mandatory app requirements to the installed-provider graph."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GATE = (ROOT / "src/native/NativeCapabilityGate.cpp").read_text(encoding="utf-8")
BRIDGE = (ROOT / "src/native/NativeProviderCapabilityBridge.cpp").read_text(encoding="utf-8")
MANIFEST = json.loads((ROOT / "Apps/model_viewer.json").read_text(encoding="utf-8"))
DOC = (ROOT / "docs/APP_CAPABILITY_REQUIREMENTS.md").read_text(encoding="utf-8")

assert {"capability": "input.touch.raw", "api": ">=1"} in MANIFEST.get("requires", [])
assert "input.touch.raw" not in [entry.get("capability") for entry in MANIFEST.get("optional", [])]

ready_start = GATE.index('extern "C" bool native_app_capabilities_ready')
bind_start = GATE.index('extern "C" bool native_app_capabilities_bind')
release_start = GATE.index('extern "C" void native_app_capabilities_release')
ready = GATE[ready_start:bind_start]
bind = GATE[bind_start:release_start]

assert "captureInstalledCapabilities()" in ready
assert "versionInInstalledSnapshot" in ready
assert "resolveRequirementWithInstalledProvider" in ready
assert "installed=%lu" in ready

assert "RuntimeInstalledProviders::acquireCapability" in bind
assert "registryRequirements" in bind
assert "installedBindings.leases" in GATE
assert "releaseInstalledBindings" in GATE
assert "Resource::Dependencies" in bind

assert 'const char* keys[] = {"requires", "optional"}' in BRIDGE
assert "declaredCapability(capability, version)" in BRIDGE
assert "declaredOptional" not in BRIDGE

assert "installed provider" in DOC.lower()
assert "activate" in DOC.lower()

print("Mandatory installed-provider requirement/version validation contract PASS")
