# U1 implementation ledger

PR: [#96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96), branch `impl/u1-riscrte`. This ledger records pushed source, not inferred completion or hardware qualification.

## Branch integration

- `0a8c21c`: merged current `master` (`1ebfbfe`) into `impl/u1-riscrte`. The merge preserved U1's generic package-builder/test versions where master still carried the fixed USB version whitelist, and incorporated the newer board-power/VBUS diagnostics, platform version, tests and firmware 1.2.33/1.2.34 artifacts.
- GitHub compare after the merge reported the U1 branch **0 commits behind master**. No PR merge, tag, release or flash was performed.

## Stream / USB source landed

- Generic ELF byte endpoints, grants/revoke/connectAcross, records compatibility and external provider I/O outside the stream mutex. `open_usb` stays unsupported in favor of semantic `serial.port` RX/TX.
- Installed USB class ELF binding, checked teardown and retryable semantic leases retain token/table/package pin on uncertain physical close.
- `ClassStreamSession` and `NativeStreamBridge` retain RX/TX chunks across buffer backpressure, short/zero writes and retry; no accepted/unwritten byte suffix is intentionally discarded.
- Duplicate class-session attachment was removed so `NativeSerialPortBridge` establishes physical ownership before attaching the published pair.
- I²C/VBUS ownership and board-power provider source are present. USB controller ELF/IDF linkage remains unresolved U1 work.

## Generic ordinary package path landed

- Schema-1 ordinary manifest/preflight/stage/transaction engine with four kinds: application, driver, service, provider.
- Stored `.rte.zip` bootstrap validates bounded ZIP structure, contiguous local entry topology and every CRC before ordinary SHA-256/ABI/import verification and recoverable publication.
- `PackageOrdinarySdZipAdapter` provides the production SD archive entrypoint and preserves unknown files.
- `NativeOnlineOrdinaryCatalog` downloads one bounded generic `package-catalog.json`, retains its immutable release tag and never uses `latest` again after selecting an archive.
- `NativeOnlineRtePackageInstall` downloads one selected `.rte.zip` to `/Packages/Inbox`, pins catalog size/SHA-256, then invokes the same SD ZIP transaction.
- `NativePackageManagerBridge` exposes directory, ZIP and online generic package operations with caller-kind filtering. `fbc5b8e` authorizes both legacy flat management apps and their canonical `/sd/Apps/<id>/<artifact>` managed-package launch paths; metadata cannot widen privileges.

## App Store / release integration landed

- `befe901`: App Store release view now uses `t5_package_manager_api_v1::online_*`; it no longer calls legacy `app_catalog_refresh/get/download` for online installation. SD directory/ZIP installation remains on the same package-manager API.
- `41b5e5a`: App Store host fixture now exercises the generic online catalog/install ABI rather than the legacy app catalog.
- `f0fbca2`: `build_all_apps.py` stages every built native app as an ordinary application package containing its ELF, app sidecar and `.package.json`, while temporarily retaining the aggregate legacy app catalog for older firmware.
- `5f28827`: generic release export now accepts canonical underscore IDs and rejects a real release that omits either application or driver packages.
- `01bedb8`: release catalog contract test verifies archive SHA/size, package identity/inventory and requires application + driver presence.
- `12431f7`: release workflow executes that generic release-catalog contract before any publication. Firmware preservation/staging logic remains intact.

## Actual validation / limits

- GitHub confirmed the branch writes and merge ancestry. Recent source suites and firmware have **not** been executed on these commits; the local container still cannot obtain a repository checkout through normal Git network access.
- No current green CI, firmware build, physical USB, VBUS or package-install hardware result is claimed.
- The release workflow was edited only; it was not run or manually dispatched.
- Legacy App Store release code remains in `NativeAppHost` as a compatibility ABI, but the U1 App Store no longer uses it for normal online installation.
- Driver Manager's UI/native bridge still uses legacy driver/USB catalog discovery for its normal online path. Its recovery UI remains useful and should be retained while moving catalog/install underneath it to the generic package index.

## Remaining U1

1. Convert Driver Manager online discovery/install to the generic pinned package catalog/ZIP path without losing recovery handling or dependency behavior; then remove normal dependence on `driver-catalog.json`, `usb-provider-catalog.json` and latest-release loose driver assets.
2. Run/fix stream, native-app and package host suites; repair compile/link defects found by real builds. Validate shutdown/disconnect/quiescence boundaries.
3. Remove package-only P-256/signed RISC-PKG/trust-policy/security-floor production paths while preserving ordinary SHA-256, TLS, ABI/import authorization and rollback.
4. Complete per-ID/nested-resource migration for every package kind, normalize CDC lineage to stable `usb-cdc-acm`, and retain bounded migration support for known legacy installs.
5. Eliminate redundant installed-ELF whole-file hashing on normal launch and resolve USB controller IDF linkage / remaining T5UsbApi compatibility ownership. No U2/U3/U4 scope.

## Next source action

Move Driver Manager catalog/install onto `RuntimeOnlinePackages::Catalog` + `OrdinaryZip` while preserving its recovery surface. Then qualify the changed host suites and firmware build when an executable checkout/CI path is available.
