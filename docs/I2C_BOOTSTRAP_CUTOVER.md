# I²C bus ELF: stable provider API and dependency-safe bootstrap cutover

**Normative, September 18, 2026; specification only.** This is the owner-authorized, narrow I²C implementation exception to [hardware-agnostic boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) during U1–U3. [Shared chip and rail ownership](SHARED_CHIP_AND_RAIL_OWNERSHIP.md) is mandatory for *all physical chip addresses, expander pins and shared rails* reached over this bus. The same bus-ELF-first, internal firmware backend temporarily, native ELF later pattern governs [SPI/UART](SPI_UART_ELF_BOOTSTRAP_CUTOVER.md). Peripheral ELFs may never call temporary firmware I²C directly. No code/hardware qualification is implied.

## Stable architecture from U1 onward

```text
USB host ELF --requires--> BQ VBUS chip-owner ELF --requires--> i2c.bus
Optional USB dependency --> PCA chip-owner ELF ----requires--> i2c.bus
U3 EPD sequencer ---> TPS chip-owner ELF + PCA pin grants ---requires--> i2c.bus
U3 GNSS/LoRa ---> shared radio rail owner ---> PCA IO00 grant ---> i2c.bus
                                                            ^
                                                   installed I²C bus ELF
                                                            |
                                    U1–U2: private firmware raw bus backend
                                    U3:    native ELF controller/transfers
```

**Bus versus device:** one independently installed I²C bus ELF publishes the versioned `i2c.bus` interface with bounded transactions, instances, concurrency/error behavior, rights and lifecycle. It alone may delegate raw controller setup/transfers to firmware until U3. Bus admission must permit **one exclusive claim per fitted chip instance/address** and generation-safe transaction handles, but the bus ELF does **not** implement PCA/TPS/BQ registers, chip identification policy, VBUS, EPD-HV or radio rail sequencing. Individual **single-owner chip ELFs** implement registers, probes, interrupts, power policy and safe teardown, then publish any number of scoped capabilities to independent clients. Generic core arbitrates opaque claims only, never parses I²C addresses or chip names. No dual PCA register owners for buttons and power, dual TPS EPD/power owners, or separate BQ charger and VBUS register owners; see [shared chip contract](SHARED_CHIP_AND_RAIL_OWNERSHIP.md).

ELF clients depend on the bus capability, never `kernel.i2c`, `Wire` or private firmware symbols. Firmware never interprets device identity/registers, publishes a competing normal bus or replaces an absent ELF. Only bus-level backend proxy is allowed, never device-specific proxy. A power sequencer may consume one TPS owner's capability and granted PCA pins without directly claiming either chip's register file.

## Dependency and startup invariants

Verify actual T5 Pro revision/schematic, pin routing, boot-alive rails, safe defaults, chip address and complete directed power graph. A bus owner cannot require a power chip whose start itself requires the unavailable bus without an independently verified bootstrap. **The BQ25896 `0x6B` USB OTG VBUS branch and TPS65185 `0x68` EPD-HV branch share this I²C controller but are distinct chips, and TPS is not automatically a USB prerequisite.** PCA9535 `0x20` is one chip with separately granted button, display and shared-radio pins; do not create two PCA owners or require all its downstream functions at once. The actual U1 USB power chain includes PCA only if verified hardware depends on it. Document graph with resource owner, capability, consumers and rail prerequisites before activation; fail closed on cycle/missing component or unsafe power.

Keep exactly one physical I²C controller owner; while transitional, firmware executes raw I/O solely under the bus ELF's delegation and its serialization. No global `Wire` to device ELFs, repeated independent controller init, unbounded transaction, persistent callbacks or recovery fallback. Genuinely required isolated boot/module-store/recovery must release its controller before ordinary ELF ownership and never publish a second normal bus.

## Narrow transitional implementation (U1–U2)

Firmware raw primitives live in a named CPU/board port compatibility module outside core registry, UI, streams, package manager, USB and chip policy. Version and allowlist imports **only to the actual I²C bus ELF**. Limit to bounded acquire/release/setup, combined address/write/read with finite timeouts, errors, buffer/retry limits, cancellation and cleanup. No firmware C++ objects, `Wire` globals, chip behavior, hidden reinit or indefinite callbacks. Bus ELF translates consumer operations into primitive calls and owns client admission/instance generations. Independently enforce generic execution-context grants. Record package ID, public ABI, private symbols, exclusive importer, controller ownership and U3 removal. Existing ELF builds do not prove compliance until actual symbols, manifests and activation are inspected.

Build/package/install bus ELF first in U1, then actual BQ VBUS and any verified PCA USB prerequisites, then USB providers. U1 non-USB witness may consume the installed bus/chip capability, never a private firmware primitive. Missing/disabled bus or chip owner means its dependent capabilities are unavailable with no firmware rescue. Fully native controller logic is **not** a U1 gate.

## Milestone gates and exclusive handoff

**U1:** generic streams/registry/rights/lifecycle first; stable installed I²C bus ELF sole firmware I²C importer; **assign one chip/register owner to each actually needed USB power component** and arbitrate BQ OTG/charge and any PCA pin masks; then functional USB ELFs. Test importer restriction, duplicate physical address/output grants, absent chip/bus, bounded transactions, graph and safe VBUS shutdown. EPD TPS and shared radio rail are not U1 implementation gates unless hardware evidence establishes an actual USB dependency.

**U2:** independent publishing retains bus and chip-owner package IDs, interfaces and actual resource claims. No duplicate owner via a different package name or per-device firmware import.

**U3:** migrate verified remaining chip owners and consumers to existing `i2c.bus`: one PCA register owner serving button/EPD/radio pin grants, one TPS register owner serving EPD HV/VCOM, one BQ owner serving VBUS and charging, plus a single shared radio rail policy owner. No duplicate register access or raw PCA IO00 from GNSS/LoRa. Confirm noncyclic startup and full multi-client leases; then block bus admissions, quiesce consumers, drain/cancel, revoke generations, safe-state rails, release/deinit firmware controller and switch the *same bus ELF's internal backend* to native controller hardware. Rebind using unchanged public interface; no overlap, firmware fallback or unsafe unmap. Remove normal firmware I²C imports and adapter; boot-only recovery only when proven necessary. SPI/UART cutovers follow their separate contracts without device firmware imports.

**U4:** CAM provisioning/image uses fitted providers and minimal ROM/port, never retired T5 adapters or universally required T5 chip packages.

**U1 acceptance:** installed bus exists and is only firmware I²C importer; BQ/optional PCA chip owners and USB use its public interface with exclusive chip claims, safe VBUS and no fallback. **U3 acceptance:** native I²C ELF exclusively operates controller with unchanged public bus ABI; one PCA/TPS/BQ owner each, allocated pins/shared rail correct, no normal firmware I²C imports or fallback; prior consumers work without bus API rewrite. Hardware proof remains owner qualification.
