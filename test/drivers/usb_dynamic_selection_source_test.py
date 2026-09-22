#!/usr/bin/env python3
"""Guard U1's production semantic-serial cutover.

This is a source invariant only, not build or hardware evidence. Installed
serial.port providers must own discovery/matching and the production app path
must resolve exact provider announcements without a compiled USB dispatcher,
USB-specific semantic projection, or USB-class stream shuttle.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
usb = (root / 'src/native/NativeUsbBridge.cpp').read_text()
classes = (root / 'src/native/NativeUsbClassBridge.cpp').read_text()
serial = (root / 'src/native/NativeSerialPortBridge_implementation.inc').read_text()
stream1 = (root / 'src/native/NativeStreamBridge.p1.inc').read_text()
stream2 = (root / 'src/native/NativeStreamBridge.p2.inc').read_text()
stream3 = (root / 'src/native/NativeStreamBridge.p3.inc').read_text()
selector = (root / 'src/runtime/drivers/InstalledProviderSelector.h').read_text()
session = (root / 'src/runtime/drivers/InstalledProviderSession.h').read_text()
graph = (root / 'src/runtime/drivers/InstalledProviderGraph.cpp').read_text()
abi = (root / 'sdk/driver/RiscUsbProviderV1.h').read_text()
serial_abi = (root / 'sdk/driver/RiscSerialPortV1.h').read_text()
manager = (root / 'src/runtime/drivers/InstalledSerialInventory.h').read_text()
serial_session = (root / 'src/runtime/drivers/InstalledSerialSession.h').read_text()
publisher = (root / 'src/runtime/capabilities/SerialProviderDevices.h').read_text()

# Hardware-blind generic runtime components cannot grow transport knowledge.
for source in (selector, session, manager, serial_session, publisher):
    for forbidden in ('RiscUsbControllerV1.h', 'RiscUsbProviderV1.h',
                      'nativeUsb', 'USBREF', 'VBUSREF', 'usb_host_',
                      '"usb-host-v2"', '"usb-cdc-acm-v2"',
                      '"usb-cp210x-v2"', 'vid ==', 'switch (vid)'):
        assert forbidden not in source, f'transport-specific core dependency: {forbidden}'

assert 'nextProviderChecked(capability, api, cursor, id, sizeof(id))' in selector
assert 'nextProviderChecked("serial.port", RISC_SERIAL_PORT_API_V1' in manager
assert 'recoverFailedProvider(failedProvider_, capability_, version_)' in session
assert 'recoverFailedFrom(providerId, capability, version)' in graph
assert 'risc_serial_port_inventory_v1' in serial_abi
assert 'risc_usb_serial_class_inventory_v1' in abi
assert 'slot.devices->refresh(' in manager
assert 'slot.devices->withdrawAll()' in manager
assert 'recoverFailedProvider(slot.id,' in manager
assert 'registry_.get(handle, &live)' in publisher
assert 'InstalledSerialSession' in serial_session
assert 'providerDevice_' in serial_session and 'providerGeneration_' in serial_session

# The production T5UsbApi symbol is now explicitly non-hardware. Historical
# orchestration may remain below #else for host fixtures only.
assert usb.startswith('#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)')
prod_usb = usb[:usb.index('\n#else\n')]
assert 'return nullptr;' in prod_usb
for forbidden in ('nextProvider(', 'nativeUsbClass', 'serialStart(',
                  'hostGrant', 'reconcile()', 'RiscUsbControllerV1.h'):
    assert forbidden not in prod_usb, f'production legacy USB hardware path: {forbidden}'

# NativeUsbClassBridge may retain an opt-in legacy selector for host fixtures,
# but production must not enable it implicitly.
assert '#if defined(RISCRTE_ENABLE_LEGACY_USB_CLASS_SELECTOR)' in classes
assert '#if defined(ESP_PLATFORM)' not in classes
assert 'installedClass.select(' in classes

# Production semantic serial path resolves exact provider-owned inventory and
# binds a hardware-blind session. The legacy branch below #else may still exist
# temporarily for non-ESP regression fixtures.
generic_start = serial.index('#if RISCRTE_INSTALLED_SERIAL_PATH\nbool installedLeaseRevoked()')
generic_end = serial.index('\n#else\nbool leaseRevoked()', generic_start)
generic = serial[generic_start:generic_end]
for required in ('installedSerial.resolve(', 'installedSession.bind(',
                 'installedSession.open(', 'nativeStreamOpenSerialPair(',
                 'RuntimeDevices::systemRegistry().acquire('):
    assert required in generic, f'missing production semantic serial step: {required}'
for forbidden in ('nativeUsbClass', 'nativeStreamOpenUsbPair(',
                  'UsbSerialProjection', 'devices.resolve('):
    assert forbidden not in generic, f'USB-specific production serial path: {forbidden}'

# Production stream shuttle is semantic serial and contains no USB class/device
# bridge dependency. Host-only USB-named aliases may remain in p3 under guard.
for source in (stream1, stream2):
    for forbidden in ('NativeUsbClassBridge', 'NativeUsbDeviceRegistry',
                      '<T5UsbApi.h>', 'nativeUsbClass', 'nativeUsbProvider',
                      'pairUsb', 'pendingUsb', 'usbTxRetry'):
        assert forbidden not in source, f'USB-specific semantic stream shuttle: {forbidden}'
assert 'nativeSerialProviderRead' in stream1
assert 'nativeSerialProviderWrite' in stream1
assert 'serialReadProvider' in stream2 and 'serialWriteProvider' in stream2
assert 'nativeStreamOpenSerialPair' in stream3
assert 'nativeSerialProviderEpoch()' in stream3
alias = stream3.index('#if !defined(ESP_PLATFORM) && !defined(ARDUINO_ARCH_ESP32)')
assert stream3.index('nativeStreamOpenUsbPair', alias) > alias

# Actual class ELFs still own transport matching and provider-originated
# inventory, with no physical claim/control merely to discover a match.
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

print('U1 provider-originated semantic serial source boundary: PASS')
