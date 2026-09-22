# USB OTG Host, Hub, and Device-Function Provider Architecture

## Status and authority

**Normative USB provider specification.** Governed by [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md), especially the mandatory [HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md). Detailed external-hub, multi-device topology, transfer-concurrency, resource-accounting and power requirements are defined by [USB Hub and Multi-Device Host Support](USB_HUB_SUPPORT.md). The earlier claim that the T5/RiscRTE framework owns a USB core, controller, role management, topology, transfers, endpoints or VBUS is **superseded**. Existing firmware code implementing these functions is **CURRENT/LEGACY, NONCOMPLIANT** and must migrate to installable ELF providers. This file describes how the USB provider ecosystem should work; it does not put USB awareness into RiscRTE.

## 1. Architectural invariant

The RiscRTE core knows only that a module may request a capability whose identifier is `usb`, `usb.host`, `usb.device`, `serial.port`, `storage.removable`, or another arbitrary name. These identifiers receive exactly the same generic registry, resolver, execution-context, lease, stream and event treatment as unrelated names. **No code in the normal compiled core may identify a USB controller, contain ESP-IDF USB host/device APIs, power VBUS, enumerate devices, parse descriptors, select a class/chipset, submit a USB transfer, or perform USB recovery.** The core never needs a `USB_ROLE_*` enum or a USB-specific resource manager.

Installable **USB provider ELFs own the complete USB hardware and protocol implementation**, composed as several modules when appropriate. A USB-host-controller ELF may publish `usb` and `usb.host`; a USB-device-controller ELF may publish `usb.device`; class/function ELFs consume these interfaces and publish semantic capabilities. Their provider SDK is USB-aware; the core resolver is not. Software may request precisely `usb` if that is the contract it needs. Applications should prefer a more semantic capability such as `serial.port` if they do not need USB-specific operations.

```text
serial_monitor.elf -> serial.port
                          ^ provides
                 usb_cdc_acm.elf
                    | requires usb.host
                    v
              usb_host.elf -------- requires board.power.vbus (if needed)
                    |                                  ^
                    |                            board_power.elf
                    v
              USB controller / device

RiscRTE core only resolves names, rights and lifecycles between these ELFs.
```

Another ELF can provide `serial.port` over UART/BLE without affecting the framework. A previously unsupported USB chipset can be installed as its own ELF without changing the compiled firmware or central provider dispatch.

## 2. Hardware and board validation

Before activating a board-specific USB provider, verify D+/D- and connector wiring, VBUS sensing/source/switch, maximum safe source current, reverse-current and backfeed protection, externally powered hubs, charging/system-power interactions, ESD protection, and adapter/role limitations. These constraints belong in an installed board profile and the responsible USB and power provider implementations, not in RiscRTE core conditionals. A USB host must refuse unsafe VBUS or role transitions based on validated profile and current hardware observations. A separate charger/power provider may export a versioned rail-control capability instead of hard-coding a charger register sequence into the USB provider.

## 3. Provider decomposition and ownership

- **Controller/host provider ELF:** owns controller initialization/shutdown, ESP-IDF or port-specific USB calls, role selection, PHY coordination, host worker(s), enumeration, address allocation, hub topology, interfaces, endpoints, transfer memory, DMA constraints, completion callbacks, device loss and recovery. Exposes a versioned capability such as `usb.host` with opaque client/device/interface/transfer handles.
- **Device-controller provider ELF:** owns device mode, descriptors, endpoint routing, device-side transfer lifetime and safe detach/reconfiguration; advertises `usb.device` or related capability. If one physical OTG controller cannot host and act as a device simultaneously, the controller provider arbitrates this constraint.
- **Host-class ELFs:** CDC ACM, FTDI, CP210x, CH34x, MSC, HID, debug probes and future protocols. Each owns matching, descriptor parsing, vendor/class requests, device configuration, data paths, error handling and teardown. Host-class modules acquire `usb.host`, not firmware-owned host handles. USB-specific interfaces are defined in USB provider SDKs, not interpreted by the core.
- **Device-function ELFs:** CDC, HID, MSC and composite functions provide descriptors/function behavior through a device-controller provider; the controller provider enforces compatible endpoint/role claims.
- **Power provider ELF:** handles charger/power-controller registers and sequences, rail constraints and safety; USB provider consumes its capability when board design requires it.

An implementation may combine compatible provider roles into one ELF; separation is for reusable capability dependencies, not an excuse to move any USB operation back to firmware. The only compiled platform support allowed is generic boot/loader/CPU/OS primitives and explicitly isolated recovery code, never a resident USB host serving normal applications.

## 4. OTG roles and arbitration

Role state (`OFF`, `HOST`, `DEVICE`, `TRANSITION`, `ERROR`) is private to the controller provider or published as USB capability metadata; it is not a core enum or branch. The provider serializes role transitions and refuses conflicting clients. Role changes coordinate device-function quiescence, host-class disconnection, transfer cancellation, power capability release, hardware switch and state publication. An application does not manipulate controller hardware directly; it calls an authorized provider capability. RiscRTE tracks generic rights and provider lifecycle only.

## 5. Host topology and discovery

A host provider models root ports, hubs, downstream ports, addresses, interfaces and alternate settings, and detects attach/detach. Example topology:

```text
USB host controller ELF
  hub A / port 1 -> FTDI
        / port 2 -> SWD probe
        / port 3 -> storage device
```

The host provider owns descriptor requests, topology identity, epoch changes, hub enumeration, port power/reset and retries. It publishes abstract device records and lifecycle events through the generic provider-to-registry API; the registry copies bounded provider-supplied identity and opaque metadata but MUST NOT interpret USB VID/PID, hub paths, endpoint addresses, class codes or protocol state. A class ELF may inspect provider-supplied descriptors and report semantic capabilities. Disconnect/replug yields new generation-qualified handles even when visible identifiers match; a former client cannot access the replacement using stale handles.

The normal core MUST NOT include `nativeDeviceDiscoveryTick()`-style USB-specific polling, `UsbSerialProjection` or a special USB registry. Such code is preserved only as a legacy implementation note pending migration.

## 6. Host capability interface

The USB host provider SDK SHOULD define versioned operations to enumerate or subscribe to devices, obtain bounded descriptor copies, claim/release interfaces and endpoints, submit/control/cancel/await transfers, get transfer status, and request safe port reset or power operations. Handles are provider-issued opaque generation-qualified tokens with explicit identity, rights, lifetime and in-flight operation tracking. Descriptor and transfer memory remain within the provider's defined lifetime or use explicitly retained buffers; no untracked cross-ELF pointers survive unload. A driver cannot fabricate a claim by guessing a token. The host provider serializes conflicts across class clients and enforces its own physical resource ownership.

A generic RiscRTE stream can move bounded bytes/records between a class provider and an application; the class provider is responsible for USB reads/writes, packetization and actual transfer completion. The kernel stream system MUST NOT implement endpoint I/O or contain special `open_usb` production logic.

## 7. Class and device-function behavior

Each class provider SHALL implement its full working protocol, not only descriptor matching/control payload encoders. For example CDC ACM owns union/functional-descriptor handling as applicable, data/control-interface choice, SET_LINE_CODING, SET_CONTROL_LINE_STATE, interface/endpoint claims via `usb.host`, bulk RX/TX, disconnect/retry and propagation of `serial.port` behavior. CP210x/CH34x/FTDI providers own their own vendor-specific initialization/configuration, baud encoding, control requests and transfer policy. MSC handles mass-storage class protocol and exports block/storage capabilities; a generic filesystem provider may consume these. HID handles report descriptors, parsing and input capability publication. A device-function provider owns its advertised function and coordinates descriptor/endpoint allocation via `usb.device`.

A generic RiscRTE resolver MUST NOT know any class identifiers, USB-specific selection priority, fallback rules, or vendor IDs. Matching priority and conflicts are handled by installed USB providers under published provider policy and capabilities. No hard-coded `usb-cdc-acm` filename/path, direct class function table, or resident CDC/CP210x/CH34x fallback in production firmware is permitted.

## 8. VBUS, electrical safety and runtime power

The USB controller provider coordinates host VBUS enable/disable through its own implementation or by consuming an installed board power capability. Charger register access, I2C transaction semantics and board-specific sequencing reside in power/bus ELFs. Generic runtime power/suspend policy may request capability suspension and receive state reports; it does not toggle USB rails or know that an app's lease implies a USB power lock. During suspend/unplug/cancellation, the owning providers drain/cancel transfers, detach class clients, release interfaces, relinquish VBUS as safe, publish capability loss and acknowledge quiescence before unload. Fail closed on uncertain hardware teardown; never treat a generic software lease release as proof of safe PHY shutdown.

## 9. Security and execution lifetime

The core checks generic app/provider identity, manifest/dependency versions, rights, consent and invocation lifetime; providers validate their own resource handles and authorized operations. A class provider obtains appropriate grants from its host provider; an untrusted app is not given host-controller authority merely because it can access `serial.port`. Provider ELFs may be privileged and require verified signatures. Dynamic ELF does not guarantee memory isolation or safe recovery after hard faults. Generic cancellation triggers provider-specific teardown; provider code cannot be unmapped while callbacks, DMA, tasks, transfers or borrowed API pointers can still reach it.

## 10. Recovery and migration

An explicitly separate ROM/bootloader/recovery path may use board-specific hardware only to ensure recovery when normal ELFs are unavailable. This path MUST NOT advertise ordinary `usb`/`serial.port` capabilities and MUST NOT silently handle production I/O after the USB driver is removed. Keep existing `NativeUsbBridge.cpp`, `UsbCdcDriverRuntime.cpp`, direct USB stream paths and resident chipset implementations under **CURRENT/LEGACY, NONCOMPLIANT** documentation until their functions have genuinely moved to ELFs. Do not add new hardware behavior to them while calling it driver modularity.

Migration order: define fully functional provider SDK/ABI; implement independent host-controller ELF with safe controller/VBUS ownership; implement class ELFs using that provider and semantic stream capabilities; switch consumers to generic capability discovery; remove special firmware USB load paths, enumeration/registry projections, transport branches and silent fallback; validate class/chipset behavior and recovery independently.

## 11. Acceptance gates

1. Build normal firmware with no operational USB/OTG/controller/class/chipset logic, USB-specific headers, USB discovery or direct stream handlers outside separately documented bootstrap-only files.
2. Add support for a new USB chipset/class by installing only its ELF, manifest and optional profile; prove real transfers, configuration and lifecycle without firmware rebuild.
3. Remove/disable the USB host/class ELF and confirm capabilities disappear; no built-in implementation takes over normal operations.
4. Exercise CDC, CP210x/CH34x where supported, hub/multiple interfaces, host/device mode constraints, VBUS safe startup/shutdown, baud/line coding, full-duplex streams, rapid unplug/replug and identity revocation.
5. Exercise device loss during active transfers, failed control requests, cancellation, teardown timeout, power failure, repeated launch/release and provider unload without stale DMA/callbacks or leaks.
6. Demonstrate the same hardware-neutral core can load an alternative provider implementing the same semantic capability. All builds and host tests must pass; hardware acceptance requires a real board.

**Failure criterion:** If a driver ELF is a proxy into compiled firmware USB behavior, if a USB class needs a new core code path, or if the framework directly owns USB hardware, this specification is not implemented.
