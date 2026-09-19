# I²C bus ELF: stable provider API and dependency-safe bootstrap cutover

**Normative, September 18, 2026; specification only.** This is the owner-authorized, narrowly scoped I²C implementation exception to [hardware-agnostic boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) during U1–U3. The same *bus ELF public interface first, internal raw firmware backend temporarily, native ELF controller later* pattern governs [SPI and UART](SPI_UART_ELF_BOOTSTRAP_CUTOVER.md), each with its **own** separately restricted importer and migration. This document does not authorize a peripheral ELF to call any temporary firmware I²C API or imply completed code/hardware qualification.

## Stable architecture from U1 onward

```text
USB host/class ELF ---> power/expander ELF ---requires---> i2c.bus
Touch/RTC/gauge ELF --------------------------requires---> i2c.bus
                                                 ^
                                       provides I²C bus ELF
                                                 |
                              U1–U2: private temporary firmware raw bus backend
                              U3: native ELF controller and transfers
```

**One provider and one public contract:** independently installed I²C bus ELF publishes versioned `i2c.bus` (or compatible existing bus contract), bounded transactions, bus instance selection, concurrency/error behavior, rights and lifecycle. Other ELFs require this capability, never `kernel.i2c`, `Wire` or private firmware symbols. Bus ELF alone may temporarily delegate raw controller setup and transfer to firmware, and owns its public capability, client admission, instance handles and arbitration throughout the migration. Firmware never interprets client device identity/registers or protocol, publishes a competing normal bus, or serves as an absent-ELF fallback. Device ELFs own actual registers/probes/power policies/recovery. Only a bus-level backend proxy is allowed, not a device-specific proxy.

## Dependency and startup invariants

Verify real T5 Pro board revision, pin routing, safe electrical defaults, boot-alive rail and device power graph. I²C power/expander ELFs may enable downstream USB rails, so full bus activation must not depend on an ELF that cannot start without `i2c.bus` unless an independent verified bootstrap exists. Firmware port primitive may handle only necessary controller/pin/clock setup, not chip policy. Draw directed graph; fail closed on cycle, missing bus ELF, corrupt package or unsafe power. Keep exactly one controller owner; during transition firmware performs raw transfers solely under the installed I²C ELF's delegation, which owns arbitration and client generations. No unbounded transaction, direct global Wire, independent controller init, persistent ELF callbacks or runtime recovery bus fallback. Truly required isolated early boot/module-store/recovery use cannot publish a second normal bus and must relinquish physical controller before ordinary ELF ownership.

## Narrow transitional implementation (U1–U2)

Locate firmware raw primitives in explicitly named CPU/board port compatibility module outside core registry, UI, streams, package manager and USB. Version and permit imports **only for the actual I²C bus ELF**, rejecting power/expander/USB/touch/RTC/gauge/apps and every other module. Limit to bounded acquire/release/controller setup and combined address/write/read transactions with finite timeouts, explicit errors, buffer and retry limits, cancellation and safe cleanup. No firmware C++ objects/`Wire` globals, device behavior, implicit reinitialization or indefinite callbacks. Bus ELF translates consumer operations to the private primitive; generic execution-context grants remain independent and correct. Record bus ELF ID, public ABI, private symbols, allowed imports, controller owner and removal obligation. Existing built ELF can be adapted after inspecting actual linked manifest/imports; asset existence is not proof of compliance.

Build, package and install the bus ELF first in U1, then make power/expander and USB dependency graph consume its normal `i2c.bus` provider. U1 non-USB witness may consume the bus ELF, not private firmware port. A missing/disabled bus ELF means bus/dependents unavailable; firmware does not rescue them. The I²C ELF's fully native controller implementation is not a U1 gate.

## Milestone gates and exclusive handoff

**U1:** first generic streams/registry/rights/lifecycle, then stable installed I²C bus ELF as the **sole** temporary firmware I²C importer, followed by dependent functional power/USB ELFs. Test import allowlist, noncyclic startup, bounded transactions, missing provider and no fallback. Native controller takeover is not a U1 gate.

**U2:** independent package publishing retains bus package ID, public capability and consumer ABI; only bus ELF may retain its private port dependency. No per-device imports or separate bus installer.

**U3:** migrate additional verified touch/RTC/charger/gauge/peripheral devices to the existing capability. Do not rewrite U1 power/expander consumers. Verify noncyclic startup, then block new operations, quiesce consumers, drain/cancel, revoke old-generation handles, safe-state rails/pins, release/deinitialize firmware controller, and exclusively start *the same bus ELF's internal native controller backend*. Rebind consumers through unchanged public API. No concurrent controllers or firmware fallback on failure; fail closed/isolate recovery and pin/quarantine uncertain provider code. Remove temporary production firmware imports and adapter entry points; separately proven isolated boot/recovery only if truly necessary, never as provider. U3 also performs corresponding independently specified SPI/UART bus cutovers, without coupling any I²C peripheral to their backend symbols.

**U4:** headless CAM provisioning/image uses installed providers and minimal ROM/port functionality, not retired T5 temporary bus calls.

**U1 acceptance:** installed I²C ELF publishes bus; USB/power/expander have zero direct firmware I²C imports and depend on public bus; only that bus ELF privately delegates bounded raw transfers; removing ELF removes capability. **U3 acceptance:** same public bus capability backed by exclusively owning native I²C ELF, no normal firmware I²C calls/imports, prior consumers continue without bus API rewrite, and no fallback. Owner alone qualifies hardware.
