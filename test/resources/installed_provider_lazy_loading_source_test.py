#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
text = (repo / "src/runtime/drivers/InstalledProviderGraph.cpp").read_text()

prepare_start = text.index("bool prepare() {")
last_error_start = text.index("const char* lastError()", prepare_start)
prepare = text[prepare_start:last_error_start]
assert "openNextFile" not in prepare
assert "registerOne" not in prepare
assert "captureInstalledCapabilities" not in prepare

acquire_start = text.index("bool acquire(const char* providerId")
release_start = text.index("bool release(Lease* lease)", acquire_start)
acquire = text[acquire_start:release_start]
assert "graph->hasProvider(providerId, capability, version)" in acquire
assert "captureInstalledCapabilities()" in acquire
assert "registerNamedProvider" in acquire

register_start = text.index("bool registerOne(")
selector_start = text.index("bool registerCapability(", register_start + 1)
register = text[register_start:selector_start]
dependency = register.index("registerCapability(destination, verified")
elf_read = register.index('"driver.elf"')
assert dependency < elf_read
assert "destination.hasProvider(id, expectedCapability, expectedApi)" in register

print("Installed provider graph: metadata-first lazy ELF admission source invariant PASS")
