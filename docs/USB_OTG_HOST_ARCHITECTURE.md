# T5S3 USB OTG, Host, Hub, and Device-Function Architecture

## Status

Architecture specification for the ESP32-S3 native USB OTG subsystem in T5S3.

This specification treats USB as a framework-owned transport subsystem supporting both **host mode** and **device mode**, including hubs, dynamically resolved host class providers, removable storage, HID, serial transports, and composite USB-device functions.

> **Core invariant:** The T5 core owns the physical USB controller, OTG role, topology, transfers, endpoint lifetime, VBUS/power policy, and resource arbitration. Pluggable modules provide class/function behavior above that boundary.

---

## 1. Goals

The USB architecture SHALL:

1. support ESP32-S3 native USB host operation where the board hardware permits it;
2. support USB hubs and multiple simultaneously enumerated downstream devices;
3. support USB device/peripheral mode;
4. support composite device configurations where practical;
5. abstract USB hardware behind capabilities rather than exposing controller internals to applications;
6. allow pluggable ELF host-class providers such as MSC, HID, CDC ACM, FTDI, CP210x, CH34x, debug probes, and future classes;
7. allow controlled USB device functions such as CDC, HID, MSC, and future functions;
8. preserve stable logical device identity across a dynamic hub topology where possible;
9. integrate USB resources with application/service/driver ownership;
10. centralize USB VBUS and power policy;
11. prevent simultaneous conflicting ownership of block storage;
12. integrate with sleep, hot-plug, memory pressure, and security policy;
13. expose generic capabilities such as `serial.host`, `storage.removable`, and `input.keyboard` independently of transport implementation.

---

## 2. Hardware assumptions and board validation

The ESP32-S3 native USB peripheral supports USB OTG functionality, but protocol support does not prove that a particular board can safely source host VBUS or switch every OTG role.

Before enabling production host mode, the T5S3 board schematic/hardware SHALL be validated for:

- native D+/D- routing;
- connector wiring;
- VBUS sensing;
- VBUS source/switch implementation;
- maximum safe source current;
- reverse-current/backfeed protection;
- behavior when externally powered hubs are attached;
- simultaneous charging/system-power interactions;
- ESD/protection components;
- connector/adapter role requirements.

The framework SHALL model the board's actual electrical capabilities rather than assuming generic ESP32-S3 capabilities.

---

## 3. Architecture

```text
Applications / Services
        |
        | generic capabilities
        v
+----------------------------------------------+
|              T5 USB Core                     |
|                                              |
| OTG role manager                             |
| host controller ownership                    |
| device controller ownership                  |
| enumeration/topology                         |
| endpoint/transfer management                 |
| VBUS/power policy                            |
| hot-plug events                              |
| provider resolution                          |
+----------------------+-----------------------+
                       |
          +------------+------------+
          |                         |
          v                         v
      HOST MODE                 DEVICE MODE
          |                         |
      USB topology             descriptor set
          |                    endpoint routing
          v                         |
 host-class providers               v
          |                  device functions
          v
 generic T5 capabilities
```

---

## 4. USB roles

The framework SHALL represent at least:

```text
USB_ROLE_OFF
USB_ROLE_HOST
USB_ROLE_DEVICE
USB_ROLE_TRANSITION
USB_ROLE_ERROR
```

Role transitions SHALL be serialized by the USB core.

Applications SHALL NOT directly switch the hardware role without going through framework policy.

Host and device operation may be mutually exclusive on the native controller. The role manager SHALL reject incompatible simultaneous requests.

---

## 5. Host topology

Host mode SHALL represent USB as a topology rather than a single attached peripheral.

Example:

```text
T5S3 Root Host
      |
      +-- Hub A
          |
          +-- Port 1: FTDI adapter
          +-- Port 2: ST-LINK/debug probe
          +-- Port 3: MSP/FET/debug probe
          +-- Port 4: USB thumb drive
```

The topology manager SHALL track:

- hub path;
- port number;
- USB address;
- VID/PID;
- device descriptor identity;
- interfaces;
- class/subclass/protocol;
- endpoint descriptors;
- optional serial number;
- current configuration;
- provider binding;
- connect/disconnect generation.

USB address alone SHALL NOT be treated as persistent device identity.

---

## 6. Stable device handles

Applications and higher-level services SHALL receive opaque framework handles rather than raw USB device pointers or addresses.

Conceptually:

```c
typedef uint32_t t5_usb_device_handle_t;
typedef uint32_t t5_usb_interface_handle_t;
```

A handle SHALL be invalidated when the underlying device disconnects or its topology generation changes.

All operations SHALL detect stale handles.

---

## 7. Hub support

Hub support is a first-class host requirement.

The USB core SHALL support:

- root-device attachment;
- one or more supported hub tiers subject to stack/hardware limits;
- per-port connect/disconnect detection;
- downstream enumeration;
- multiple simultaneously present devices;
- independent class-provider binding per interface/device;
- topology change events;
- per-port failure containment where available.

The implementation SHALL not assume the first enumerated device is the only or desired device.

---

## 8. Hub power policy

The framework SHALL distinguish externally powered and bus-powered topologies where detectable/configured.

USB host power policy SHALL account for:

- board VBUS source limit;
- declared device configuration current;
- hub consumption;
- battery state;
- external power availability;
- startup/inrush behavior;
- programming targets that may also draw power.

T5 SHOULD recommend or require an externally powered hub for multi-device programming/debugging configurations unless board validation establishes sufficient safe power capacity.

The USB software stack SHALL NOT claim that descriptor current guarantees real electrical safety.

---

## 9. Host class/provider model

The USB core owns enumeration and transfers. Pluggable providers implement device/interface semantics.

Examples:

```text
usb-host-msc.elf
usb-host-hid.elf
usb-host-cdc-acm.elf
usb-host-ftdi.elf
usb-host-cp210x.elf
usb-host-ch34x.elf
usb-host-stlink.elf
usb-host-msp-debug.elf
```

Provider matching MAY use:

- USB class/subclass/protocol;
- VID/PID;
- interface descriptors;
- provider priority;
- explicit user selection when ambiguous.

A provider SHALL declare supported match rules in authenticated metadata.

---

## 10. Generic capability projection

USB-specific implementation SHOULD disappear above the provider boundary.

Examples:

```text
FTDI / CP210x / CH34x
        |
        v
    serial.host

USB MSC
        |
        v
 storage.removable

USB HID keyboard
        |
        v
 input.keyboard

ST-LINK/JTAG probe
        |
        v
 debug.transport / program.transport
```

Applications SHOULD request capabilities, not specific ELF filenames.

---

## 11. Multi-interface devices

A single USB device may expose multiple interfaces.

The USB core SHALL permit different interface providers when valid and SHALL prevent conflicting claims.

Composite devices may expose, for example:

```text
Interface 0: CDC control
Interface 1: CDC data
Interface 2: HID
```

Provider binding SHALL understand interface associations required by class implementations.

---

## 12. Transfer ownership

The USB core SHALL own endpoint/transfer objects.

Providers receive bounded host APIs for:

```text
control transfer
bulk transfer
interrupt transfer
isochronous transfer if supported/required later
configuration/interface selection
endpoint cancellation
```

A provider SHALL NOT retain framework transfer pointers after teardown.

Disconnect SHALL cancel/revoke outstanding operations deterministically.

---

## 13. Hot-plug behavior

USB devices are inherently dynamic.

The framework SHALL publish events such as:

```text
usb.device.connected
usb.device.configured
usb.device.disconnected
usb.interface.bound
usb.interface.unbound
usb.hub.connected
usb.hub.port_changed
usb.role.changed
usb.power.fault
```

Consumers SHALL tolerate device removal during active I/O.

No higher-level operation may assume a device remains attached merely because it was present when a job began.

---

## 14. Device mode

In device mode, T5S3 acts as a USB peripheral to an external host.

Candidate functions include:

```text
CDC serial
HID keyboard
HID mouse
HID consumer controls
Mass Storage
MIDI
vendor-specific interfaces
future debug/control interfaces
```

The USB device core owns descriptors, endpoint allocation, configuration state, and host connection lifetime.

---

## 15. Device-function providers

Device-mode functions SHALL be treated as pluggable function providers rather than ordinary background services.

Conceptually:

```text
T5 USB Device Core
       |
       +-- CDC function provider
       +-- HID function provider
       +-- MSC function provider
```

The core composes compatible functions into a descriptor/configuration set before enumeration.

A function cannot be arbitrarily added after host enumeration without an intentional disconnect/re-enumeration cycle.

---

## 16. Composite device policy

The USB core MAY expose multiple compatible device functions simultaneously.

Example:

```text
T5S3 Composite USB Device

Interface(s): CDC console
Interface:    HID controls
Interface:    Mass Storage
```

The framework SHALL arbitrate endpoint/resource limits and reject incompatible configurations.

Composite configuration SHOULD be declarative and policy-controlled.

---

## 17. HID security

USB HID device emulation is security-sensitive because a keyboard-like device can inject input into a connected host.

Capabilities such as:

```text
usb.device.hid.keyboard
usb.device.hid.mouse
usb.device.hid.consumer
```

SHALL require explicit authorization appropriate to application/service trust policy.

Ordinary applications SHALL NOT silently gain HID injection capability merely because USB device mode is available.

---

## 18. Mass-storage device mode

USB MSC device mode SHALL enforce exclusive block-device ownership.

This is forbidden:

```text
T5 filesystem mounted read/write
        +
external PC writing same FAT filesystem
```

A safe ownership transition is:

```text
T5 owns filesystem
      |
unmount / flush / revoke mappings
      |
USB MSC gains block ownership
      |
external host owns filesystem
      |
host eject/disconnect
      |
T5 validates/remounts filesystem
```

Alternatively, T5 MAY expose a dedicated storage image/partition not concurrently mounted by the framework.

---

## 19. Storage-VM interaction

Before exporting an SD-backed filesystem/block device through MSC, T5 SHALL coordinate with `MEMORY_ARCHITECTURE.md`.

The transition SHALL ensure:

- dirty storage-VM data is committed or the transition fails;
- mappings against the exported filesystem are revoked/unmapped;
- no framework writer remains active;
- block ownership is exclusive.

On return, stale cached data SHALL NOT be reused without revalidation.

---

## 20. Sleep and power integration

USB host/device state SHALL participate in centralized power management.

Host mode may require VBUS and active controller state. Device mode may need to respond to external-host suspend/resume.

The USB core SHALL expose whether active topology/function requirements permit system sleep and what wake behavior is supported.

Individual providers SHALL not independently manipulate global USB power state.

---

## 21. Memory model

USB transfer buffers SHALL follow ESP-IDF/native-controller memory requirements. Providers SHALL request buffers through framework APIs that can select internal SRAM versus PSRAM appropriately.

Large file/programming payloads SHOULD be streamed from SD/storage VM through bounded transfer buffers rather than fully loaded into RAM.

Example:

```text
firmware.bin on SD
      |
64 KiB bounded read window
      |
programming protocol
      |
USB transfer buffers
      |
target
```

---

## 22. Security and signed providers

Production USB providers SHALL follow `SECURITY_ARCHITECTURE.md`.

Provider manifests SHOULD authenticate:

- module type;
- provider identity;
- supported VID/PID/class match rules;
- provided capabilities;
- required host APIs;
- ABI version;
- ELF digest;
- signer identity.

Binding a provider to a USB device does not grant arbitrary framework privilege.

---

## 23. Diagnostics

USB diagnostics SHOULD expose:

```text
current OTG role
VBUS state/policy
root/hub topology
hub path and ports
VID/PID
serial number when available
class/interfaces/endpoints
bound providers
provided capabilities
transfer errors/timeouts
power faults
connect/disconnect generation
active owners/jobs
```

A USB diagnostics view is strongly recommended before implementing complex programming workflows.

---

## 24. Testing requirements

Tests SHOULD prove:

1. host role starts/stops cleanly;
2. device role starts/stops cleanly;
3. role transitions reclaim resources;
4. a powered hub enumerates multiple simultaneous devices;
5. disconnect of one hub device does not invalidate unrelated devices;
6. stable framework handles reject stale topology generations;
7. multiple providers bind to different devices/interfaces concurrently;
8. FTDI/CDC/MSC/HID provider examples coexist on one hub where supported;
9. device mode CDC works;
10. device mode HID requires authorization;
11. MSC export enforces exclusive filesystem ownership;
12. storage-VM mappings are safely handled before MSC ownership transfer;
13. large payloads stream without full-file RAM residency;
14. USB power faults produce controlled errors;
15. sleep/power policy accounts for active USB state;
16. provider unload cancels/reclaims transfers;
17. unsigned/unauthorized providers are rejected under production policy.

---

## 25. Normative rules

1. **T5 core owns the native USB controller and OTG role.**
2. **USB topology supports multiple simultaneous devices, not a singleton peripheral assumption.**
3. **Applications receive opaque handles/capabilities rather than controller pointers.**
4. **Host class behavior is pluggable above core enumeration/transfer machinery.**
5. **USB device functions are pluggable function providers, not arbitrary app-owned endpoints.**
6. **Generic capabilities abstract transport implementation.**
7. **Hub topology and hot-unplug are normal operating conditions.**
8. **VBUS behavior follows validated board electrical limits.**
9. **Multi-device host use should prefer an externally powered hub unless hardware validation proves otherwise.**
10. **MSC block ownership is exclusive.**
11. **HID device injection is permission-controlled.**
12. **Large USB data transfers are streamed through bounded buffers.**
13. **Production provider ELFs are authenticated and authorized before execution.**

---

## 26. Implementation sequence

1. validate T5S3 native USB/VBUS hardware against schematic;
2. introduce framework-owned USB role manager;
3. implement host root enumeration and opaque device/interface handles;
4. implement hub topology and hot-plug events;
5. add USB diagnostics;
6. define host-provider ELF ABI and match metadata;
7. implement MSC and generic HID/CDC examples;
8. implement FTDI/CP210x/CH34x serial providers;
9. integrate generic capability projection (`serial.host`, `storage.removable`, etc.);
10. implement device-mode core and CDC function;
11. add HID device functions with authorization;
12. add MSC device mode with exclusive block ownership;
13. add composite-device composition;
14. integrate signed provider policy;
15. use the subsystem as the transport foundation for the multi-target programmer/debugger architecture.
