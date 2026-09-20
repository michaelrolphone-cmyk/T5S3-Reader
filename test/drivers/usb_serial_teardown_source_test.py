#!/usr/bin/env python3
"""Source boundary: USB serial stop must not tear down unrelated providers.

This is a structural regression, not a device run or proof of graph behavior.
ProviderGraphV2's host tests independently cover transitive release semantics.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
bridge = (root / 'src/native/NativeUsbBridge.cpp').read_text(encoding='utf-8')
classes = (root / 'src/native/NativeUsbClassBridge.cpp').read_text(encoding='utf-8')
graph = (root / 'src/runtime/drivers/ProviderGraphV2.cpp').read_text(encoding='utf-8')

close = bridge.split('bool closeClass() {', 1)[1].split('\n}\n', 1)[0]
stop = bridge.split('void serialStop() {', 1)[1].split('\n}\n', 1)[0]
release = graph.split('bool GraphV2::release(GrantV2 grant) {', 1)[1].split('\n}\n', 1)[0]

# An active class token with no matching session belongs to another consumer.
# An idle, lazily bound class can hold a provider grant even without a token.
assert 'if (!session && nativeUsbClassToken()) return true;' in close
assert 'if (!session && !nativeUsbClassBound()) return true;' in close
assert '(session && nativeUsbClassToken() != session)' in close
assert '!nativeUsbClassUnbindChecked()' in close
assert 'quarantined = true;' in close

# Only class and host grants belong to this connection. The graph's global
# shutdown belongs to system shutdown, not a serial app closing a port.
assert stop.index('if (!closeClass()) return;') < stop.index('RuntimeInstalledProviders::release(&hostGrant)')
assert 'if (!RuntimeInstalledProviders::shutdown())' not in stop
assert 'RuntimeInstalledProviders::shutdown();' not in stop
assert 'quarantined = true;' in stop
assert 'host = nullptr;' in stop
assert stop.index('RuntimeInstalledProviders::release(&hostGrant)') < stop.index('host = nullptr;')
assert 'running = false;' in stop and 'initialState(T5_USB_STATUS_OFF)' in stop

# A failed release must retain ownership and generation, not turn an
# uncertain hardware transition into a newly available provider slot.
assert 'slot.pendingRelease = true;' in release
assert 'if (!deactivateIfUnused(node)) return false;' in release
assert release.index('if (!deactivateIfUnused(node)) return false;') < release.index('slot.occupied = false;')
assert 'if (installedClass.grant.slot && !RuntimeInstalledProviders::release(&installedClass))' in classes

print('USB serial teardown scope: class-before-host, no global shutdown, external class protected: PASS')
