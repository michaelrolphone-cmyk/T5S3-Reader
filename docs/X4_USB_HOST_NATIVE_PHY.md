# Opt-in USB controller native PHY lease

This local integration unit extends the existing shared controller from Reader
PR464, commit `19ba15b06ca12d81c8931048c6cbf73895e3d649`. It neither replaces
the host stack nor activates X4 USB host hardware. The default controller
manifest/build remains `usb-controller-esp32s3@0.1.23`; an explicitly selected
profile uses the same package ID at `0.1.24`. These are alternative generations,
not two controllers to install together. `usb-host-v2@0.1.6` and all class drivers
are unchanged.

## Smallest reusable integration unit

`--native-phy-lease` selects `manifest.native-phy.json` and compiles
`RISC_USB_CONTROLLER_NATIVE_PHY_LEASE=1`. It retains `board.power.vbus@1` and
additionally requires `platform.usb.phy.resource@1`. Missing, duplicate, wrong
version or malformed dependencies reject without I/O. Dependency order does
not select ownership. Ordinary builds retain the original single dependency.

The exact SDK header is copied unchanged from Runtime
`5ab1f4e9e3f17efb1153966868b52660bde53be0` (0.1.90): SHA-256
`fe668ff8e8f83ea454eafd059c61c633d2213bc2a77504d8794a64c1dc11b016`.
No native ABI layout, native export or Runtime source changes are needed for
this lease adapter.

The controller claims before reading/changing the PHY route or starting the
host. Native code fences its HWCDC console, reserves GPIO19/20, and applies
its existing lifecycle barriers. The controller still owns all USB protocol,
host stack, PHY, interrupts, DMA, VBUS use and class-facing behavior.

Native release occurs only after interrupt/bulk/control/admission custody,
device claims, DMA, callbacks, host library, PHY and VBUS are safely cleared,
source-off is observed, and the prior mux route is restored. A false native
claim with a nonzero token or failed native release retains that exact token
and API binding. The outer quiescence path detects token-only retention even
when host installation never began. Failed cleanup cannot rebind or stop the
provider. A later successful release permits normal stop/unbind.

The existing .23 role/host lifecycle is otherwise preserved: role sensing
starts during provider activation; a later poll prepares the host and event
client before power acquisition and connection admission. Empty/changed-power
sessions park through the existing retained cleanup. The controller still
advertises the same 52-byte interrupt/diagnostic prefix. Its private bounded
control, bulk and admission paths remain private; complete bounded startup,
enumeration/device destruction, root-port timing and the public deadline
suffix remain unfinished upstream work.

## Actual missing X4 roots

1. **Qualified board power/role root.** Neither the selected X4 board graph nor
   this unit provides `board.power.vbus@1`. Both sourced and externally powered
   host modes remain unqualified. Keep all guessed VBUS mappings unassigned.
2. **Minimal Runtime controller admission.** Runtime .90's normal
   `ProviderModuleV2::load` uses ordinary `dlopen`; `loadVerifiedBytes` returns
   false. The existence of a private OS/CPU table does not activate that route.
   `GraphV2::addManagerValidatedPrivileged` also explicitly returns false.
3. **Diagnostic imports.** Reading the actual selected .90 firmware's lookup
   tables resolves 6 of this controller's 47 imports ordinarily. Another 38
   are in its private OS/CPU table. `printf`, `puts` and `putchar` still have no
   selected lookup binding, even if private admission were enabled. The weak
   provider diagnostic substitutions are not linked in this firmware. A
   future integration must provide an appropriate bounded diagnostic contract
   or remove those imports, without exposing broad raw OS imports to apps.
4. **Product/class binding.** Existing shared paths are
   `usb.controller -> usb.host -> usb.hid -> keyboard -> input.text`,
   HID gamepad and direct host XInput gamepad. Both gamepad classes also need
   `platform.clock`. `usb-ui-navigation` composes text and the two gamepads.
   These need explicit product grants/bindings and coexistence with the
   existing X4 navigation provider. Raw HID supports descriptor/report access;
   these source inventories contain no USB mouse-to-semantic-pointer class
   provider. Serial classes remain available on the same host root for later
   integration, with no new physical activation supplied here.

The selected .90 DIO native artifact has its PHY marker enabled. That removes
neither the other roots nor the native import/admission gaps. Static table
addresses and a controller target link are not evidence of product activation.

### Minimum existing admission contract to review next

Reader already has a transport-neutral equivalent:
`ManagerProviderCandidateV2` and
`DeviceProviderExecutorV2::registerManagerValidated` in
`src/runtime/drivers/DeviceProviderExecutorV2.{h,cpp}`. It carries exact
provider identity/capability, dependency array, owned executable bytes, length,
selected OS/CPU ABI, sorted imported-symbol set, integrity digest and package
generation/resource identity. Registration copies metadata/bytes; it does not
activate the controller or grant consumer rights. The existing sidecars are
`provider-abi.v1` and `privileged-imports.v1`, generated from the actual ELF by
`scripts/generate_provider_package_inputs_v1.py`. Neither declaration nor
checksum is import authority.

Minimal Runtime already retains `SpecV2`'s `requiredOsCpuAbi`,
`verifiedElfBytes`, `verifiedElfLength`, `declaredImports`,
`declaredImportCount`, digest and ownership storage. Its present boundaries are:

- `Runtime::prepare` creates ordinary specs and calls `graph_.addVerified`.
- `GraphV2::addManagerValidatedPrivileged` is disabled; public `addVerified`
  rejects privileged specs.
- `ModuleV2::loadVerifiedBytes` is disabled.
- `NativeBankStore::allowedImport` / `admitElf` use the ordinary public import
  set even for driver images. Provisioning, cohort admission and update checks
  must all receive the same exact selected provider policy.
- `esp_elf_relocate_privileged_verified_v1` already checks image structure,
  the versioned import inventory and equality of the complete declared/actual
  undefined set across `.dynsym` and `.symtab`, then grants a one-shot,
  task-owned relocation scope. No new USB firmware implementation is needed.

The narrow next proposal is an explicit trusted, per-provider native-import
policy for admitted cohorts, reusing those owned-image and exact-import
mechanisms. Restrict relocation to the selected provider's approved imports
intersected with the supported ABI; do not publish the private table to
ordinary apps/providers or treat a manifest/digest/capability name as a grant.
Retain Runtime .90's current stream, module-lease and failed-quiescence checks
when adapting the shared Reader path. Copying the full Reader manager or
overwriting the newer Runtime module lifecycle is unnecessary.

The three diagnostic names need bounded scoped bindings that respect the
native PHY fence, or a separate controller change removing those imports.
Negative tests must retain ordinary-app rejection, forged/undeclared imports,
changed snapshots, failed starts and retained cleanup. This proposal is not
implemented or assigned a Runtime version by the .24 unit. Board VBUS
qualification remains a separate mandatory dependency.

## Verified board evidence

The [manufacturer product specification](https://www.xteink.com/products/xteink-x4-pro-pocket-ereader)
lists pogo contacts and a magnetic charging adapter. The manufacturer's
[User Guides Center](https://www.xteink.com/pages/user-guide), X4 Pro guide
pages 2, 3 and 5, shows four contacts on the reader and USB-C on the external
magnetic adapter. This establishes charging input, not reverse power delivery.

[FreeInk engineering notes](https://github.com/Free-Ink/freeink-sdk/blob/main/docs/xteink-x4pro-support.md#rtc--usb--battery)
identify GPIO19 D-minus and GPIO20 D-plus. GPIO21 is active-high charger STAT,
not a qualified VBUS-presence signal. VBUS detection is explicitly unresolved.
[BoardConfig.h](https://github.com/Free-Ink/freeink-sdk/blob/main/libs/hardware/BoardConfig/include/BoardConfig.h)
leaves `usbDetect` unassigned; the old GPIO10 suggestion is unconfirmed and
GPIO10 is the confirmed touch interrupt. GPIO1 is a peripheral rail, GPIO2
touch power and GPIO5 SD power; none is promoted to a USB control pin.

No retrieved authoritative source establishes a USB 5 V boost/load switch,
source current limit, reverse-current protection, VBUS voltage detector,
CC/ID role circuit, adapter schematic, or supported externally powered host
topology. This is an evidence gap, not proof that such operation is impossible.
No power-control implementation or hardware action is part of this unit.

## Reproduction and evidence

Use the existing pinned ESP-IDF v4.4.7 commit
`38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`, GCC8 ESP32-S3 toolchain and
the compile-only fixture in `test/target/usb_controller_bounded`. Example:

```sh
USB_CONTROLLER_RUNTIME_SOURCE=/path/to/runtime-0190 \
  bash test/run_usb_controller_native_phy_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 \
  USB_CONTROLLER_RUNTIME_SOURCE=/path/to/runtime-0190 \
  bash test/run_usb_controller_native_phy_test.sh
python3 scripts/probe_usb_controller_esp32s3.py --link-experiment \
  --native-phy-lease --compile-database /path/to/compile_commands.json \
  --idf-source /path/to/exact-idf-v4.4.7
python3 scripts/audit_usb_controller_elf.py \
  dist/experimental/usb-controller-esp32s3-native-phy/driver.elf --strict
python3 scripts/audit_usb_controller_runtime.py \
  --controller dist/experimental/usb-controller-esp32s3-native-phy/driver.elf \
  --runtime-source /path/to/runtime-0190 \
  --runtime-elf /path/to/exact-runtime-firmware.elf --output audit.json
```

The explicit profile outputs `driver.elf` and its matching manifest in its
own experimental directory. Ordinary package builders/catalogs are unchanged.
No product profile selects this output.

Qualification performed: actual Runtime Port/materialized table plus adapter;
actual controller dependency admission, startup, outer quiescence, HID drain
gate and cleanup bodies with simulated hardware boundaries; clean and partial
native refusals, exact-token retry, lifecycle/pad exclusion, every existing
startup/teardown failure, and malformed dependency rejection. Normal and
ASan/UBSan runs pass. Independent source review found no blocking defect.
This is section-level source integration, not the full controller executing
inside the Runtime on hardware.

Target compile/link and Reader's strict audit pass: 47 imports, 878 supported
relocations, no USB firmware imports and unchanged 52-byte capability prefix.
The default build's allocated sections exactly match the pre-edit .23 build;
debug/source-line metadata may differ. Existing 85 control, 49 admission and
31 bulk scenarios pass normally and under ASan/UBSan. Shared HID, role, host
and deadline regressions pass. OS/HAL boundaries, multicore scheduling and
electrical behavior remain unqualified.

Controller .24 was reserved locally after live master, relevant branch/tag and
open-PR checks on 2026-10-09: master and highest controller tag were .20,
PR464 remained .23, and no .24 claim was found. No publication, Runtime version,
product change, flash or physical test was performed.
