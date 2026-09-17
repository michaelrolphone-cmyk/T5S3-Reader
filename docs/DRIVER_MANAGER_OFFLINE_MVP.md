# Driver Manager — offline MVP

## Requirement

Driver Manager MUST start, display installed driver packages, and install a locally staged driver while Wi-Fi is disabled, missing credentials, DNS is unavailable, or GitHub is down. Online catalog discovery is an explicitly selected optional view and MUST NOT gate the local view. A failed online refresh MUST leave the local view accessible.

## SD layout

```text
/Drivers/
  gps-nmea/
    manifest.json
    driver.elf
  usb-cdc-acm/
    manifest.json
    driver.elf
  Inbox/
    gps-nmea/
      manifest.json
      driver.elf
    usb-cdc-acm/
      manifest.json
      driver.elf
```

Installed driver directory names and inbox directory names MUST exactly match their manifest `id`. They are single directory components: lowercase ASCII letters, digits, `_` and `-`, under 64 bytes. `Inbox` is reserved for staged packages. Hidden installer directories are not displayed as installed drivers. ZIP release archives must be extracted to an inbox subdirectory on a computer; the device does not implement DEFLATE decoding.

## UI and behavior

1. Open directly to **Local SD drivers**. Enumerate the installed driver folders and `Inbox` without opening a network connection.
2. Display each installed package ID, declared version, capability and integrity status. Installed does not mean loaded, active, or bound to hardware.
3. Display each inbox package and identify invalid manifest/ELF combinations. Invalid items are visible for diagnosis but cannot be installed.
4. Selecting a valid inbox package copies its ELF into an isolated temporary staging file and invokes the canonical driver installer. Do not rename or destroy the inbox source package. Existing installed packages must survive validation and failed-install errors.
5. Install and update use the same path. Driver activation is unchanged until the runtime next binds or starts it.
6. The separate **Online release catalog** action invokes the existing aggregate/legacy discovery only when explicitly selected. Failure must not close Driver Manager or erase the local inventory.
7. The Back/Home controls must exit or return to local mode without waiting on an unbounded network operation.

## Security and resource boundaries

All filesystem enumeration, package validation and installation are firmware-owned API actions. The app ELF receives bounded inventory metadata, never arbitrary VFS access. Manifest fields are validated before use as path components. Check the ELF header, declared size and SHA-256 before and after staging using the existing installer. Reject mismatched folder identities; do not traverse or follow externally supplied paths. Bound inventories to 64 items and manifests to 4 KiB.

## Outstanding for full offline lifecycle

A remove operation requires a trustworthy proof that no live provider still maps the ELF. Rollback requires keeping the last validated installed package; the existing installer presently discards its prior-version backup. Neither action should be offered until its safety/lifecycle contract and tests exist. Inventory and offline validated install/update are the first deliverable of this document, not a claim of complete offline package-lifecycle management.

## Acceptance tests

- Wi-Fi turned off: Driver Manager starts and lists valid installed GPS/USB packages.
- Empty `/Drivers`: displays an empty local inventory and online option without crashing.
- Valid inbox package: appears and installs without network traffic; source inbox files remain unchanged.
- Valid older installed version + newer inbox: update replaces installed package; driver does not become active automatically.
- Missing manifest, mismatched directory ID, bad ELF header, truncated ELF, incorrect SHA or size: visible as invalid and rejected without modifying the installed version.
- Failed SD read/write during staging: install fails, previous installation remains; subsequent local refresh still works.
- Online catalog times out: error is visible, navigation back to local inventory works, no app exit or forced restart.
- Repeated local refresh and app exit/reopen: no leaks, stale selected item or network dependency.

## Follow-up gate

Safe offline uninstall and persistent rollback must be finished before calling the entire driver package-manager lifecycle feature complete.
