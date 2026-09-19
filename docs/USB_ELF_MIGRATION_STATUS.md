# USB and I²C ELF migration — current implementation and acceptance

**As of September 18, 2026; current-state document, not physical qualification.** The binding target is [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md) and [HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md). The authoritative next execution sequence and single owner-test handoff is [NEXT_HARDWARE_TEST_MILESTONE.md](NEXT_HARDWARE_TEST_MILESTONE.md), workstreams A–F. [PACKAGE_MANAGER_SCOPE_CONTRACT.md](PACKAGE_MANAGER_SCOPE_CONTRACT.md) prohibits making signed-package machinery a driver prerequisite. Earlier draft statements in this file about PR #78 being unmerged, provider ELFs unpublished, or ordinary admission unimplemented are historical and are superseded here.

## Normative destination

Normal RiscRTE firmware neither implements nor understands USB hardware, driver identity, host topology, VID/PID, class matching, USB sessions, controller/PHY, VBUS/charger, I²C0 bus arbitration, hotplug, direct USB streams or fallback. It handles opaque manifest-declared capabilities, generic device records/events, execution-context grants, versioned dispatch and provider lifetimes. Actual controller, host, class, power, bus and chipset behavior resides entirely in independently installed functional ELFs. The Serial Monitor requests generic `serial.port`, reads/writes a provider-owned stream and uses provider-advertised serial configuration controls. Removing a driver makes its capability disappear. Adding a compatible chipset requires only ELF/manifest/profile installation, never a core or board rebuild. Only explicitly isolated boot/ROM/debug primitives needed to start or recover the runtime may remain in a platform port, and these cannot register normal hardware capabilities or silently take over.

## Current implemented foundation (software, not owner-tested USB)

Merged PRs #76, #78 and #79 introduced an ordinary unsigned four-kind package engine, installed provider graph/private executor, strict candidate-ELF and imported-symbol checks, private metadata/executable-byte copies, and a 46-symbol generic privileged OS/CPU ABI. The controller and I²C ELF probes incorporate their ESP-IDF HAL/PHY/I²C implementation into the ELF rather than calling an in-firmware device implementation. The U1 baseline is seven packaged hardware-owning provider ELFs and published v1.2.19 assets; availability of those assets is **not** demonstration of electrical or physical function.

Current provider chain (dependency order, not firmware-enforced device hierarchy):

```text
usb-cdc-acm-v2 / usb-cp210x-v2 : serial.port@1
              -> usb-host-v2 : usb.host@1
                   -> usb-controller-esp32s3 : usb.controller@1
                        -> board-power-t5s3-v2 : board.power.vbus@1
                             -> i2c-esp32s3-v2 : i2c.bus@1
                                  -> platform-clock-v1 : platform.clock@1
```

- `Drivers/usb_controller_esp32s3/driver.cpp` links real ESP32-S3 host/OTG/PHY/interrupt/control/bulk/DMA code. Source-level link and audit success is not proof of IRQ/DMA draining, backfeed protection or role safety.
- `Drivers/usb_host_v2` performs enumeration, device identity generation and USB claims; publish its arrivals/removals through generic provider-originated registry events rather than firmware snapshots.
- `Drivers/usb_cdc_v2` owns CDC matching, alternate/interface claims, line coding and bulk I/O. `Drivers/usb_cp210x_v2` owns vendor-specific control and data. Qualify CP210x using exact supported VID/PID/profile rather than vendor ID alone; ambiguous composites require explicit policy/selection.
- `Drivers/i2c_esp32s3_v2`, `Drivers/board_power_t5s3_v2` and `Drivers/platform_clock_v1` provide bus, VBUS/charger and timing requirements. Compiled `Wire` ownership of I²C0 must not coexist with the activated I²C provider. Real VBUS/current, NACK/fault and thermal/electrical behavior remain for the owner's test.
- `InstalledProviderGraph` and `DeviceProviderExecutorV2` use a privately checked module/metadata snapshot, independent privileged import policy and conservative quiescence; none of this is a publisher signature or physical isolation.

## CURRENT/LEGACY, NONCOMPLIANT — USB firmware residue

`src/native/NativeUsbBridge.cpp` no longer contains its own ESP-IDF USB host controller implementation, but it still imports USB-specific ABI headers, acquires `usb-host-v2` by name, selects `usb-cp210x-v2` for VID `0x10c4` and `usb-cdc-acm-v2` otherwise, interprets descriptor-provided identities, tracks physical device/session and USB line coding, polls devices, reads/writes USB streams and guards activation with `BOARD_T5S3_PRO`. Moving the physical controller into an ELF did **not** remove these hardware/protocol choices from firmware.

The firmware paths `src/native/NativeSerialPortBridge.cpp`, `UsbSerialProjection.h`, `NativeUsbDeviceRegistry`, the `nativeDeviceDiscoveryTick`/`ActivityManager::loop` call path, USB-specific portions of `NativeStreamBridge`, `src/runtime/drivers/UsbCdcDriverRuntime.cpp` and `T5UsbApi` bindings must be inspected for **actual compile/link reachability and callers** before migration/removal. Their generic registry, stream and execution-context functionality should survive; USB-specific projection, fixed loader, session/lease, class, discovery or adapter code must not remain as a renamed resident bridge. A device and provider never get physical permission just by appearing in generic inventory. Identify debug/boot-only USB independently before deleting files; it is not production host hardware.

`src/native/NativeDriverManagerBridge.cpp` currently downloads `usb-provider-catalog.json` from a hard-coded latest-release URL and overlays physical entries on legacy driver catalogs. `NativeOnlineDriverInstall.h` uses a separate four-file canonical driver download contract with `/latest`. `scripts/build_installed_usb_stack.py` hard-codes seven providers and generates that catalog; `scripts/export_canonical_driver_release.py` requires seven packages and exports the catalog. These are **release/discovery coupling**, distinct from hardware ownership, but must be removed for an install-only modular platform. Use one generic all-kind manifest-derived catalog and common source adapter as specified by [U1 workstream B](NEXT_HARDWARE_TEST_MILESTONE.md#workstream-b--finish-the-generic-manager-and-remove-usb-catalog-coupling). The catalog is not loader authority, a dependency of offline activation, or a chipset binding rule.

## Required completion, ordered without interim owner tests

1. Finish common four-kind ordinary package inventory/source/catalog and ensure actual installed/online/offline flows do not require the USB catalog or signing experiment. Purge package-signing code after reference and generic-function audits; keep SHA-256 corruption detection and independent privileged import checks.
2. Have the functional host/class/power/bus ELFs publish generic devices/capabilities and bind/probe within their own code. Serial Monitor uses `serial.port` capability/stream/configuration with no USB dependency or board-specific selection. Support competing providers, loss/rebind and version/permission checks generically.
3. Eliminate compiled USB host/class/VID/PID/session/line coding, firmware discovery tick/projection, fixed USB ELF route and direct USB I/O. Audit normal firmware's actual link map, imports, build source lists and transport-specific conditionals; delete dead legacy files only after all consumers are migrated. Allow only tightly identified boot-only port code without normal-provider fallback.
4. Complete actual-source unit/integration tests for missed/corrupt package, dependency graph, install/remove with driver absent, module pin/use gate, double open, hotplug generation, stream lifecycle, teardown/quarantine, power/I²C exclusivity and a new independent `serial.port` provider. Execute release-equivalent firmware, app, driver builds and source/link audits, recording exact head/results.
5. Supply **one** hardware procedure for USB CDC with the known test ESP32-S3-CAM, CP210x when available, long-lived VBUS and serial logs, baud/DTR/RTS, hotplug, open/close, driver remove/reinstall, no compiled fallback, USB role/current safety and reboot/recovery. These remain **PENDING PHYSICAL TEST** until the owner runs them; CI green does not mark them PASS.

**Stop condition:** Do not call USB migration complete, ask the owner for repeated piecemeal tests, or tag/release/merge on a code-only partial result. The current implementation is **physical ELF foundations implemented; complete USB extraction, manager/catalog decoupling and physical verification outstanding**. Continue work on the same U1 branch/PR across prompts; do not wait idly on CI between independent steps. This USB milestone does not itself remove every other hardware family from firmware or prove that an ESP32-S3 binary can run on a different ESP architecture without a distinct bootstrap/ABI port.
