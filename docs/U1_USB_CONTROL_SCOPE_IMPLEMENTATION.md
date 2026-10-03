# U1 USB host control ownership — committed implementation

**Status:** Implementation on PR #96 (`impl/u1-riscrte`), September 21, 2026. This describes committed source and tests wired into runners, **not a passed current-head build, hardware qualification, or U1 completion**. Read [U1 implementation ledger](U1_IMPLEMENTATION_LEDGER.md), [USB remediation](USB_CONTRACT_VIOLATION_REMEDIATION.md), and [package identity policy](PACKAGE_IDENTITY_VERSION_POLICY.md).

## Boundary and authority

A discovered USB device token identifies an enumerated device but does **not** authorize control traffic on behalf of a particular class. The earlier `usb.host` implementation checked whether any class had claimed a device/interface, then allowed `host.control(device, ...)` with a caller-supplied device token. On composite devices this could authorize control for a different class. An interface claim and its generation, rather than device visibility, are now mandatory for class-specific control.

The provider-only `risc_usb_host_discovery_v1` structure appends `control_claim(context, claim, type, request, value, index, payload, length, timeout)` after its original host/poll/devices/release_checked prefix. No compiled firmware USB implementation was added. The original `host.control(device, ...)` function stays at its ABI offset but rejects calls; it cannot authenticate the class. The installed CDC-ACM, CP210x and CH34x class ELFs require the extended structure size and the checked-release and claim-control functions before activation.

The `usb.host` ELF accepts `control_claim` only while the exact claim token is live, not closing, its device is still present and the event stream is not faulted. It validates bounds and payload, then forwards to the physical USB controller ELF. An interface-recipient request (`bmRequestType & 0x1f == 1`) must select exactly the interface owned by that claim and cannot use a high-byte index. A device-recipient request (`recipient == 0`, including CH34x vendor commands) requires that claim to be the device's **sole** current claim, including any pending-release/quarantined claims. Endpoint and other recipients fail closed; a separate scoped extension would require distinct authorization and tests. No class can borrow the claim of a different class merely because it knows the public device token. A still-live claim is not a general permission to send an arbitrary other class's command: provider-specific class protocol enforcement remains in the class ELF, with the host enforcing the resource boundary.

CDC uses its control-interface claim for line coding and DTR/RTS, retaining the separate data-interface claim for bulk I/O. CP210x uses its claimed vendor interface for UART enable/disable, framing, baud, modem lines and the existing checked-release/detach recovery. CH34x uses its exact claim for version/initialization, baud/framing and inverted DTR/RTS device-recipient vendor commands. These operations cannot run before claim acquisition or after a failed release marks the claim closing. Retry of `release_checked` uses the same token; a physical failure does not release the dependent provider prematurely.

## Source and regression wiring

- `sdk/driver/RiscUsbControllerV1.h`: append-only provider ABI declaration.
- `Drivers/usb_host_v2/driver.c`: legacy control disabled and exact-claim checks; package `usb-host-v2` 0.1.3.
- `Drivers/usb_cdc_v2/driver.c`: exact control claim, package `usb-cdc-acm-v2` 0.1.2. The current package ID remains a **separate outstanding identity-migration blocker**; do not silently rename installed user data.
- `Drivers/usb_cp210x_v2/driver.c`: exact UART claim, package `usb-cp210x-v2` 0.1.3.
- `Drivers/usb_ch34x_v2/driver.c`: exact vendor command claim, package `usb-ch34x-v2` 0.1.2.
- `test/drivers/usb_host_control_scope_test.c` exercises the production host ELF with two composite interfaces: old device-token rejection, wrong interface/recipient denial, exclusive CH34x device control, multi-claim refusal, restoring exclusivity after verified release, stale claims and failed-release quarantine. Existing host, CDC composite, CP210x, CH34x and two-/three-ELF fixtures were adapted to the append-only API; `test/run_usb_host_v2_test.sh` now executes the composite test, and existing runners exercise the class drivers.

## Still unverified or unfinished

Do not equate committed test code or a previous green master workflow with verification of this head. Run host/class suites, the full PlatformIO/ELF build, exact import/relocation and canonical-package checks, and later owner-controlled physical tests on one release-equivalent commit. This change does **not** move USB enumeration, device arrival/removal, class probe/bind or semantic `serial.port` publication out of compiled `NativeUsbBridge`; those remain U1 core work. Exclusive console/PHY transfer, CDC stable-ID migration, structured diagnostics and master backmerge/reconciliation also remain incomplete. Do not merge, release, flash or request intermediate hardware qualification on the strength of this isolated change.
