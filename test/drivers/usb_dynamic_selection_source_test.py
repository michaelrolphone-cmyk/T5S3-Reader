#!/usr/bin/env python3
"""Source boundary guard for installed USB serial class discovery.

Behavioral production provider and firmware builds are independent acceptance
requirements. This check prevents accidental fixed ID dispatch and verifies
that probing happens inside an installed ELF BEFORE firmware opens a session.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
usb = (root / 'src/native/NativeUsbBridge.cpp').read_text()
classes = (root / 'src/native/NativeUsbClassBridge.cpp').read_text()
selector = (root / 'src/runtime/drivers/InstalledProviderSelector.h').read_text()
session = (root / 'src/runtime/drivers/InstalledProviderSession.h').read_text()
graph = (root / 'src/runtime/drivers/InstalledProviderGraph.cpp').read_text()
abi = (root / 'sdk/driver/RiscUsbProviderV1.h').read_text()

for source in (usb, classes, selector, session):
    for forbidden in ('"usb-host-v2"', '"usb-cdc-acm-v2"',
                      '"usb-cp210x-v2"', 'vid ==', 'switch (vid)'):
        assert forbidden not in source, f'fixed provider selection: {forbidden}'

assert 'nextProvider("usb.host", 1' in usb
assert 'matchingProviderId' in graph
assert 'installedClass.select(' in classes
assert '"serial.port", 1, cursor, probeInstalled' in classes
assert 'nextProvider(capability, api, cursor, id, sizeof(id))' in selector
assert 'acquire(id, capability, api, &candidate)' in selector
assert 'decision == CandidateDecision::Fault' in selector
assert 'if (!release(&candidate))' in selector
assert 'recoverFailedProvider(failedProvider_, capability_, version_)' in session
assert 'recoverFailedFrom(providerId, capability, version)' in graph
assert 'risc_usb_serial_class_discovery_v1' in abi
assert 'discovery->probe(observedDevice)' in classes
assert 'result < 0' in classes and 'CandidateDecision::Fault' in classes
assert 'installedClass.faulted()' in classes
assert 'nativeUsbClassBound() && !nativeUsbClassUnbindChecked()' in usb
assert 'nativeUsbClassBindNextInstalled(&cursor, &faulted)' in usb
assert 'if (faulted)' in usb and 'error(-1207)' in usb
assert 'nativeUsbClassToken() ||' in usb and '!nativeUsbClassUnbindChecked()' in usb

start = usb.index('bool openClass(')
end = usb.index('void reconcile()', start)
body = usb[start:end]
assert body.count('openBoundClass(token, vid, pid)') == 1
assert 'nativeUsbClassAvailable()' not in body, 'side-effectful lazy probe'
assert 'nativeUsbClassObserveDevice(token);' in body
assert body.index('nativeUsbClassObserveDevice(token);') < body.index(
    'nativeUsbClassBindNextInstalled(&cursor, &faulted)')
assert 'if (faulted)' in body, 'failed activation misreported as no match'

for name in ('cdc', 'cp210x', 'ch34x'):
    driver = (root / f'Drivers/usb_{name}_v2/driver.c').read_text()
    assert 'static int32_t probe_device(uint64_t device)' in driver
    probe = driver[driver.index('static int32_t probe_device('):driver.index(
        'static uint64_t open_device(', driver.index('static int32_t probe_device('))]
    assert 'host->configuration(' in probe and 'return parse(' in probe
    assert 'host->claim(' not in probe and 'command(' not in probe
    assert 'risc_usb_serial_class_discovery_v1 capability' in driver
    assert 'probe_device\n};' in driver

print('U1 installed-class probe/source boundary regression PASS')
