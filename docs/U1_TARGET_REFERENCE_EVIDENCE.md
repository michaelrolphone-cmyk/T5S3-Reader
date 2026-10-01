# U1 production target reference evidence

The normal PlatformIO run **36835619954** passed requested U1 head
**c80bdee158eb7ef98096c7b000cd92f339c7442b**. Its PR merge checkout was
**8b9ef127be12078af839e71c053bed70a5a1bb1d**. Compact reports were generated
from those linked ELF files during the build, independently downloaded and ZIP
SHA-256 checked. They include relevant source hashes and actual disassembly,
literal values, and function pointers in referenced API/provider objects.

| Target | Compact artifact | ZIP SHA-256 | Linked firmware ELF SHA-256 |
|---|---|---|---|
| t5s3-pro |11148579544|34e4df42c36aa9c0a7292cc38a08714737b2e98e4d8ef899d11251f4fc31dceb|f4fb709dabd464bff98d7dac6205a50c2677c72f2d1d7ddda62b7712ccb31500|
| lilygo-epd47-s3 |11149201740|dbf525867449e64368605c83b345000499efdde4ea7a402f9269eb1c5875e09b|8571476cdfef0adb59bb1ce56120835b54fc977eabf14b2b96f254b7fe4015c4|

Both targets establish the same selected production path:

- `t5_serial_port_get_api` calls the original getter; source and referenced
  diagnostic table preserve the API prefix and replace acquisition with
  `diagnosticAcquire`, which directly calls `acquirePort`
- The original getter's literal references identify its actual API object,
  including acquire/configure/status/control/release pointers
- `acquirePort` calls `RuntimeSerial::Registry::acquire` and
  `ensureUsbRegistered`. Despite its historical name, the latter registers the
  actual `installedProvider` object, whose acquisition pointer is
  `acquireInstalled`; its remaining callbacks are installed equivalents
- `acquireInstalled` references `InstalledSerialInventory::refresh`, `resolve`
  and `attachEndpoint`, plus execution-context/device lease checks. Source pins
  exact installed provider generations and attaches their published RX/TX
  endpoints; it contains no legacy raw-USB acquisition fallback in production
- `t5_usb_get_api` returns null. `openUsb` returns unsupported or denied after
  authorization; its only observed direct call is the authorization helper
- No linked symbol matched the specifically searched legacy set:
  usbAcquirePort, UsbSerialProjection, NativeUsbDevices,
  nativeUsbDirectStreamClaim, nativeUsbProviderAttach, nativeUsbClassRead,
  nativeUsbClassWrite, usb_host_install or hcd_port_init

These observations close the selected target API-to-installed-registry/table
connection and disabled compatibility entrypoint checks. They are more than
symbol presence/absence, but do not resolve every indirect callback, establish
complete graph reachability, or qualify physical console/host PHY restoration.
Those remaining claims require their own existing-path evidence. Provider
read/write and owner polling deliberately use function pointers.

The report's original direct-call display truncated one nested-template helper
name to `40u`; inspection of its retained disassembly identifies
`RuntimeDevices::Registry::copy<40u>`. The decoder now preserves nested-template
names, with a regression for subsequent necessary builds. None of the named
API/registry/provider connections above depends on that truncated helper.

Earlier bd9b09a7 compact reports remain valid narrower evidence. This report
never accessed the separately blocked74b0 full firmware artifact and makes no
claim about its bytes. No release, flash or physical execution occurred.

## Integrated candidate: controller cleanup and selected indirect boundaries

The current source adds a controller profile to the same bounded report, emitted
from the actual PIC-linked controller ELF in its existing build. It selects the
exported getter, start/stop/quiesce, role service, startup, PHY release/capture/
restore and event entrypoints that survive target optimization. Missing inlined
symbols are not interpreted as missing behavior. Firmware reports additionally
select installed configure/control/release and checked lease cleanup. The new
reports remain pending until this exact candidate's target builds finish.

Source boundaries for the deliberately indirect production path:

- InstalledSerialSession validates the versioned serial API, binds its exact
  device/generation and retains a failed-close token. Its endpoints callback
  supplies distinct RX/TX handles; installedAcquirePort attaches those exact
  handles through InstalledSerialInventory/InstalledProviderGraph, rather than
  falling back to read/write firmware USB transfers
- ProviderModuleV2::activateMapped validates the loaded t5_driver_get identity,
  capability/API and lifecycle table, binds a provider-owned stream context,
  and retains the mapping on failed quiescence. ModuleV2::poll calls the loaded
  poll suffix only while active with consumers; GraphV2::poll supplies the
  bounded owner-task dispatch reached by nativeProviderOwnerTick
- Class ELFs register their own capability/stream/poll tables. The unchanged
  real loaded host/class integration fixture exercises CDC, CP210x, CH34x and
  the fourth witness. This is executable host evidence for selected dynamic
  edges, not proof of every possible externally installed provider

The new controller_quiesce_path_test.py extracts and compiles the unchanged
production quiesce_host body with failing IDF boundary ports. It covers thirteen
failure/timeout cases, outstanding claims, unknown/source-active VBUS, faulted
controller cleanup and clock wrap. Assertions preserve outstanding handles and
PHY-route capture on failure, retry consumed versus retained resources, keep
waits finite and restore the saved route only after DMA/host/PHY/VBUS cleanup
and source-off observation. Existing production HostStartup and RoleSwitch
fixtures cover partial startup and role retry ordering. No controller behavior,
version or electrical policy changes; this closes an actual-source test gap.

Physical mux write/readback, SDK/interrupt timing and attached-cable behavior
remain qualification evidence. Neither selected call reports nor these fault
ports establish universal dynamic reachability or physical restoration success.

**Implementation In Progress**
