#!/usr/bin/env python3
"""Static regression for the U1 USB provider-selection adapter.

This is NOT a hardware test, ELF linking check or full USB extraction gate.
The behavioral graph test separately checks candidate enumeration and grants.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
usb = (root / 'src/native/NativeUsbBridge.cpp').read_text()
classes = (root / 'src/native/NativeUsbClassBridge.cpp').read_text()
graph = (root / 'src/runtime/drivers/InstalledProviderGraph.cpp').read_text()

for source in (usb, classes):
    for forbidden in ('"usb-host-v2"', '"usb-cdc-acm-v2"',
                      '"usb-cp210x-v2"', 'vid ==', 'switch (vid)'):
        assert forbidden not in source, f'fixed USB provider selection: {forbidden}'

assert 'nextProvider("usb.host", 1' in usb
assert 'nextProvider("serial.port", 1' in classes
assert 'matchingProviderId' in graph
assert 'RuntimeInstalledProviders::acquire(id, "serial.port", 1, &grant)' in classes
assert 'nativeUsbClassBound() && !nativeUsbClassUnbindChecked()' in usb
assert 'nativeUsbClassBindNextInstalled(&cursor, &faulted)' in usb
assert 'if (faulted)' in usb and 'error(-1207)' in usb
assert 'installedClass = grant;' in classes and 'candidateFault = true;' in classes
assert 'if (candidateFault || (installedClass.grant.slot && !valid(ops)))' in classes
assert 'nativeUsbClassToken() ||' in usb and '!nativeUsbClassUnbindChecked()' in usb

start = usb.index('bool openClass(')
end = usb.index('void reconcile()', start)
body = usb[start:end]
assert body.count('openBoundClass(token, vid, pid)') == 1, 'duplicate class probe'
assert 'nativeUsbClassAvailable()' not in body, 'side-effectful lazy probe'
assert 'if (faulted)' in body, 'failed activation misreported as no match'

print('U1 USB candidate source contract: generic discovery, single probe and fail-closed teardown PASS')
