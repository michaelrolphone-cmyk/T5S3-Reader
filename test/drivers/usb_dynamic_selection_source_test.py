#!/usr/bin/env python3
"""Guard actual installed-provider USB class source and semantic publication.

No production build is inferred from this source check. Classes must own
matching/inventory, the graph must retain exact grants, and generic core may
not gain chipset/USB host dispatch when adding a fourth installed class.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
usb = (root / 'src/native/NativeUsbBridge.cpp').read_text()
classes = (root / 'src/native/NativeUsbClassBridge.cpp').read_text()
selector = (root / 'src/runtime/drivers/InstalledProviderSelector.h').read_text()
session = (root / 'src/runtime/drivers/InstalledProviderSession.h').read_text()
graph = (root / 'src/runtime/drivers/InstalledProviderGraph.cpp').read_text()
abi = (root / 'sdk/driver/RiscUsbProviderV1.h').read_text()
serial_abi = (root / 'sdk/driver/RiscSerialPortV1.h').read_text()
manager = (root / 'src/runtime/drivers/InstalledSerialInventory.h').read_text()
publisher = (root / 'src/runtime/capabilities/SerialProviderDevices.h').read_text()

for source in (usb, classes, selector, session, manager, publisher):
    for forbidden in ('"usb-host-v2"', '"usb-cdc-acm-v2"',
                      '"usb-cp210x-v2"', 'vid ==', 'switch (vid)'):
        assert forbidden not in source, f'fixed provider selection: {forbidden}'
for source in (selector, session, manager, publisher):
    for forbidden in ('RiscUsbControllerV1.h', 'RiscUsbProviderV1.h',
                      'nativeUsb', 'USBREF', 'VBUSREF', 'usb_host_'):
        assert forbidden not in source, f'USB-specific core dependency: {forbidden}'

assert 'nextProvider("usb.host", 1' in usb
assert 'matchingProviderId' in graph
assert 'installedClass.select(' in classes
assert '"serial.port", 1, cursor, probeInstalled' in classes
assert 'nextProviderChecked(capability, api, cursor, id, sizeof(id))' in selector
assert 'nextProviderChecked("serial.port", RISC_SERIAL_PORT_API_V1' in manager
assert 'acquire(id, capability, api, &candidate)' in selector
assert 'decision == CandidateDecision::Fault' in selector
assert 'if (!release(&candidate))' in selector
assert 'recoverFailedProvider(failedProvider_, capability_, version_)' in session
assert 'recoverFailedFrom(providerId, capability, version)' in graph
assert 'risc_usb_serial_class_inventory_v1' in abi
assert 'risc_serial_port_inventory_v1' in serial_abi
assert 'slot.devices->refresh(' in manager
assert 'slot.devices->withdrawAll()' in manager
assert 'recoverFailedProvider(slot.id,' in manager
assert 'registry_.get(handle, &live)' in publisher
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
    probe_start = driver.index('static int32_t probe_device(')
    inventory_start = driver.index('static bool snapshot_devices(', probe_start)
    open_start = driver.index('static uint64_t open_device(', inventory_start)
    probe = driver[probe_start:inventory_start]
    inventory = driver[inventory_start:open_start]
    assert 'host->configuration(' in probe and 'return parse(' in probe
    assert 'host->claim(' not in probe and 'command(' not in probe
    assert 'probe_device(' in inventory and 'return false;' in inventory
    assert 'host->claim(' not in inventory and 'command(' not in inventory
    assert 'RISC_SERIAL_TRANSPORT_USB' in inventory
    assert 'static const risc_usb_serial_class_inventory_v1 capability' in driver
    assert 'snapshot_devices\n};' in driver

print('U1 installed class ELF-owned inventory and generic source boundary: PASS')
