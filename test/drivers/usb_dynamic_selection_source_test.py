#!/usr/bin/env python3
"""Source boundary guard for manifest-selected USB serial class providers.

Behavioral provider tests remain necessary: this is not an ELF/hardware PASS.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
usb = (root / 'src/native/NativeUsbBridge.cpp').read_text()
classes = (root / 'src/native/NativeUsbClassBridge.cpp').read_text()
selector = (root / 'src/runtime/drivers/InstalledProviderSelector.h').read_text()
graph = (root / 'src/runtime/drivers/InstalledProviderGraph.cpp').read_text()

for source in (usb, classes, selector):
    for forbidden in ('"usb-host-v2"', '"usb-cdc-acm-v2"',
                      '"usb-cp210x-v2"', 'vid ==', 'switch (vid)'):
        assert forbidden not in source, f'fixed provider selection: {forbidden}'

assert 'nextProvider("usb.host", 1' in usb
assert 'matchingProviderId' in graph
assert 'selectNext(' in classes
assert '"serial.port", 1, cursor, probeInstalled' in classes
assert 'nextProvider(capability, api, cursor, id, sizeof(id))' in selector
assert 'acquire(id, capability, api, &candidate)' in selector
assert 'decision == CandidateDecision::Fault' in selector
assert 'if (!release(&candidate))' in selector
assert 'if (selected.grant.slot) installedClass = selected;' in classes
assert 'if (candidateFault || (installedClass.grant.slot && !valid(ops)))' in classes
assert 'nativeUsbClassBound() && !nativeUsbClassUnbindChecked()' in usb
assert 'nativeUsbClassBindNextInstalled(&cursor, &faulted)' in usb
assert 'if (faulted)' in usb and 'error(-1207)' in usb
assert 'nativeUsbClassToken() ||' in usb and '!nativeUsbClassUnbindChecked()' in usb

start = usb.index('bool openClass(')
end = usb.index('void reconcile()', start)
body = usb[start:end]
assert body.count('openBoundClass(token, vid, pid)') == 1, 'duplicate class probe'
assert 'nativeUsbClassAvailable()' not in body, 'side-effectful lazy probe'
assert 'if (faulted)' in body, 'failed activation misreported as no match'

print('U1 USB candidate source contract: generic runtime enumeration and fail-closed teardown PASS')
