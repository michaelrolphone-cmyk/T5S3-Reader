# RiscRTE Hardware-Agnostic Runtime and Driver Ownership Contract

## Authority and status

**Normative target architecture.** Read after [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md) and before any transport, board, peripheral, driver, registry, stream or migration spec. Historical firmware USB, GPS, UART, BLE, SPI, I²C and power ownership is not precedent for new development. **The sole temporary exception** is [I²C Bus ELF Bootstrap and Cutover](I2C_BOOTSTRAP_CUTOVER.md): during U1–U2 and early U3, one installed I²C bus ELF may delegate raw bus operations to a restricted firmware port primitive. Its public bus capability is present from U1; **no other driver ELF may import or call the temporary firmware I²C API.** U3 replaces that bus ELF's internal implementation, then removes the temporary runtime firmware calls. This overrides contradictory older wording permitting direct temporary imports in power/expander/peripheral ELFs and requiring fully native I²C in U1. Consult [RUNTIME_DRIVER_IMPLEMENTATION.md](RUNTIME_DRIVER_IMPLEMENTATION.md) for actual migration status; a specification alone changes no code.

## 1. Hardware ownership boundary

**The RiscRTE core MUST NOT know what USB is.** `usb` and all other capability IDs are opaque dynamically registered names. The core must not contain transport-specific enumeration, protocol state machines, controller/endpoint/host handles, class selection, VID/PID matching, resets, register maps, power sequencing, bus arbitration, recovery or hard-coded drivers. No capability-name-specific core dispatch for `usb`, `serial.port`, `i2c.bus` or any other device.

A functional device ELF owns discovery/probing, matching, protocol/register behavior, device power policy, data transfers, interrupts/callbacks, hotplug, recovery, synchronization and teardown. It may consume subordinate capabilities published by other ELFs: e.g. USB CDC depends on USB host; a power/expander ELF depends on an I²C bus ELF. Driver authority is distinct from generic runtime authorization/contexts. The bus provider owns client mediation and the public bus interface, and ultimately controller I/O.

**Only the I²C bus ELF has a transitional implementation exception:** it must be an independently packaged installed provider publishing its normal versioned bus capability. Until U3, that ELF alone may import an explicitly restricted temporary firmware port ABI for bounded controller setup and raw transfers. It supplies the same bus ABI to its consumers before and after takeover. The port adapter must not identify peripheral chip models, implement their registers or policy, select USB drivers, publish a competing bus capability or rescue an absent bus ELF. Power, expander, touch, charger, RTC, USB and all other device ELFs MUST acquire the bus ELF capability and MUST NOT directly import `kernel.i2c`, `Wire` or the temporary firmware API. During transition, the port adapter holds raw controller ownership only on behalf of this single bus ELF; device policy remains in individual ELFs. This is a bounded root-bus bootstrap dependency, **not** a precedent for thin proxy device drivers.

A driver that only encodes a control packet or forwards peripheral behavior into compiled firmware fails the installable-driver requirement. Successful ELF packaging alone does not establish modularity. The temporary *bus-level* delegation above expires by U3 Work Complete.

## 2. What remains in the core and port

The core may implement ELF loading/linking, manifest/version validation, package transactions, generic capability/dependency resolution, execution contexts, opaque handles, permission policy, lifecycle supervision, resource bookkeeping, memory/executable memory, scheduling, bounded streams/events, module-store VFS and generic recovery. It handles identifiers as names, not transport classes. Core leases authorize software; they are not physical device ownership. On generic lifecycle calls the providing ELF performs safe hardware teardown before unmapping. Unknown quiescence requires pin/quarantine, not hopeful unload.

A narrow platform bootstrap/port is necessary for CPU, loader, memory and module-store/recovery. Such code must be marked boot-only and cannot silently act as a normal hardware provider or fallback. System-call shims may supply CPU/RTOS primitives without implementing device semantics. The **only** transitional hardware-bus syscall allowed in the normal runtime is the I²C primitive privately imported by the I²C bus ELF, described above. A different MCU may need a different CPU/port ABI; a new compatible peripheral must need only installed ELF/profile changes.

## 3. Provider graph

Capabilities are versioned names and interfaces published by installed providers. Manifests express required dependencies. The resolver uses IDs, graph edges, rights and lifecycle without parsing their spelling. A representative dependency graph is:

```text
serial_monitor.elf --requires--> serial.port
                                 ^
                            usb_cdc.elf --requires--> usb.host
                                                        ^
                                          usb_host_controller.elf
                                                     |
                                             board.power.vbus
                                                     ^
                                           board_power.elf
                                           --requires--> i2c.bus
                                                           ^
                                                  i2c_bus.elf
                                                           |
                                          U1–U2 private port primitive
                                          U3 ELF-owned controller operations
```

Actual manifests, board dependencies and rail ordering must be verified; this graph is illustrative, not proof of the fitted board power tree. The I²C bus ELF must start from a noncyclic set of boot-alive CPU/port prerequisites, never from a power ELF which itself requires `i2c.bus`. A board profile is provider input, not firmware USB/I²C selection policy. Drivers or independently installed providers interpret descriptors, addresses, endpoint topology, rails and chips. An alternative `serial.port` provider must need no core changes.

**New hardware on a supported CPU/port must not require a firmware recompile, built-in enum/switch/bridge, privileged exception by device name or custom installer.** The temporary I²C primitive is a time-limited internal dependency of one bus ELF, not a model to extend for other devices.

## 4. Registry, authorization and shared hardware

The Unified Device Registry accepts bounded provider-published records/events and validates copied identity, generation, rights, owner, sequence and leases. It does not poll USB, parse USB descriptors, synthesize USB devices or interpret vendor IDs. Drivers probe hardware and publish/unpublish their own records. Event observation alone grants no device access.

Execution-context leases are software permissions; providers mediate hardware contention and actual physical sessions. Shared controllers are reached through one provider capability or opaque platform resource grant, not parallel independent controller initialization. The I²C bus ELF arbitrates all consumer requests in both phases; in the transitional phase its private firmware backend performs serialized raw transfers on the one physical controller. No direct `Wire` or concurrent I²C owner. Platform resource tokens may enforce exclusivity without teaching the resolver protocol details.

Driver-only privileged ABI normally exposes generic CPU/RTOS services such as memory, tasks, synchronization, time, interrupts and opaque resources. Only the installed I²C bus ELF may have the temporary I²C import; privileged import policy must reject that symbol for apps and every other ELF. Do not add a `kernel.usb.host`, `kernel.i2c` dependency in peripheral manifests, or another firmware device implementation. ELF modules are not inherently memory-isolated; permission checks, import policy, cancellation and accounting remain necessary. Package signing is outside current owner scope.

## 5. ABI, lifecycle and I²C replacement

Use a generic versioned root driver entrypoint, manifest requires/provides and provider-owned interfaces; standard lifecycle as applicable includes probe/start/suspend/resume/stop/remove and failure publication. Provider code and dependent handles must remain pinned until quiescent, with generation-safe revocation on loss. Generic streams carry bounded data; drivers implement actual physical data production and consumption. USB descriptor, transfer and control operations belong in USB ELFs, not firmware.

The I²C bus provider's public ABI is stable across the transition. U3 changes **only the I²C ELF's private backend** from calls into the temporary firmware adapter to ELF-owned controller setup, transfers, recovery and teardown through appropriate CPU/RTOS primitives. Before switching, stop new admissions, drain/cancel transactions, quiesce clients, release the firmware controller, then exclusively claim it in the ELF. Invalidate old-generation handles and rebind through the same public capability. Never run both implementations simultaneously or silently restart a firmware fallback. No blanket consumer ELF rewrite is permitted merely to change the bus backend.

## 6. USB migration acceptance

Inventory actual linked USB source at kickoff. Legacy firmware USB host, sessions, transport-specific stream bridge, provider selection and chipset handling are migration targets, not accepted architecture. U1 USB completion requires:

1. No normal firmware USB host/class/chipset logic, device projection/provider selection, USB stream implementation or fallback. The private I²C port primitive supporting the **installed I²C bus ELF only** is the sole approved normal-runtime hardware-bus exception.
2. USB controller/host/class ELFs own host mode, enumeration, topology, interface/endpoint claims, matching, transfers, hotplug and recovery; power ELF owns charger/expander device behavior while consuming `i2c.bus` from the I²C ELF. No USB/device-specific forwarding into firmware.
3. A compatible new USB class/chipset loads as a package/profile alone; disabling its ELF removes its capability, revokes handles, quiesces hardware and leaves no compiled fallback.
4. Test providers, disconnect/reconnect, activation failure, cancellation, resource conflicts, controller timeouts, safe teardown and missing/corrupt package. Missing I²C bus ELF must prevent dependent power/USB activation, not trigger direct firmware I²C.
5. Static/source/link/import audits reject operational USB in firmware and **reject temporary I²C symbols from every ELF except the identified bus ELF**. Document the remaining one-call-boundary adapter and U3 removal obligation; do not mistake a completed USB cutover for completed I²C ownership.

## 7. Migration rules and acceptance distinction

Apply this contract and [I²C Bus ELF Bootstrap and Cutover](I2C_BOOTSTRAP_CUTOVER.md) to the platform spec, U1/U3 milestones, driver architecture, roadmap and applicable SDK/device specs. In legacy implementation documents state target first and mark current noncompliant implementations explicitly. Never restore hardware-specific core managers or let a USB/charger/touch driver bypass the bus provider. The temporary bus ELF proxy is the *only* permitted exception to complete root-bus implementation ownership; all peripheral operations reside in their own ELFs.

**U1 gate:** stable I²C bus ELF exists, is the sole client of bounded temporary firmware I²C primitives and supports the real dependent ELF stack; USB is independently modular. **U3 gate:** the same bus capability is now served by native hardware-owning I²C ELF internals, normal firmware I²C adapter and its imports are gone, and consumers operate without a bus API change. If removing a peripheral ELF leaves its device operating via firmware behavior, extraction failed. Physical hardware success still requires owner qualification.
