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

**Implementation In Progress**
