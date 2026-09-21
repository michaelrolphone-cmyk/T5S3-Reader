#!/usr/bin/env python3
"""Structural regression for scoped USB teardown and provider-owned discovery.

Not a hardware test or a substitute for the production ELF host suite.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
bridge = (root / 'src/native/NativeUsbBridge.cpp').read_text(encoding='utf-8')
classes = (root / 'src/native/NativeUsbClassBridge.cpp').read_text(encoding='utf-8')
graph = (root / 'src/runtime/drivers/ProviderGraphV2.cpp').read_text(encoding='utf-8')
host = (root / 'Drivers/usb_host_v2/driver.c').read_text(encoding='utf-8')

close = bridge.split('bool closeClass() {', 1)[1].split('\n}\n', 1)[0]
stop = bridge.split('void serialStop() {', 1)[1].split('\n}\n', 1)[0]
reconcile = bridge.split('void reconcile() {', 1)[1].split('\n}\n', 1)[0]
release = graph.split('bool GraphV2::release(GrantV2 grant) {', 1)[1].split('\n}\n', 1)[0]

# A live token without a matching bridge session belongs to another consumer;
# an idle, lazily bound class may also own a provider grant.
assert 'if (!session && nativeUsbClassToken()) return true;' in close
assert 'if (!session && !nativeUsbClassBound()) return true;' in close
assert '(session && nativeUsbClassToken() != session)' in close
assert '!nativeUsbClassUnbindChecked()' in close
assert 'quarantined = true;' in close

# Quarantine stops new I/O, NOT exact-owner cleanup retries. No early return
# is permitted before checked class close and the host grant release. Reset
# quarantine only after both phases succeed and the host pointer is cleared.
assert 'if (quarantined) return;' not in stop
assert stop.index('if (!closeClass()) return;') < stop.index('RuntimeInstalledProviders::release(&hostGrant)')
assert 'if (!RuntimeInstalledProviders::shutdown())' not in stop
assert 'RuntimeInstalledProviders::shutdown();' not in stop
assert 'quarantined = true;' in stop
assert stop.index('RuntimeInstalledProviders::release(&hostGrant)') < stop.index('hostSnapshot = nullptr;')
assert stop.index('hostSnapshot = nullptr;') < stop.index('quarantined = false;')
assert 'running = false;' in stop and 'initialState(T5_USB_STATUS_OFF)' in stop

# Firmware does not poll physical USB or fetch/parse raw configuration bytes.
# The installed host ELF alone owns poll/identity and retains the token when
# descriptor identification transiently fails.
assert 'hostSnapshot->snapshot(' in reconcile
assert 'host->poll(' not in bridge and 'host->devices(' not in bridge
assert 'configurationDescriptor' not in bridge
assert 'host->host.configuration(' not in bridge
assert 'if (!devices[i].identified) continue;' in reconcile
assert 'if (devices[i].token == device)' in reconcile
assert 'if (!poll_devices(ctx, 16, &processed)) return false;' in host
assert 'entry.token = devices[i].token;' in host

# Failed provider release must retain ownership and generation. Do not unload
# unrelated installed drivers when this one app closes its serial port.
assert 'slot.pendingRelease = true;' in release
assert 'if (!deactivateIfUnused(node)) return false;' in release
assert release.index('if (!deactivateIfUnused(node)) return false;') < release.index('slot.occupied = false;')
assert 'if (installedClass.grant.slot && !RuntimeInstalledProviders::release(&installedClass))' in classes

print('USB serial teardown retry and provider-owned host snapshot source: PASS')
