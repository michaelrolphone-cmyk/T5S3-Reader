# U1 implementation ledger

PR: [#96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96), branch `impl/u1-riscrte`, originally based on master `52226bc` after spec PR #86 merged. This ledger distinguishes source actually pushed from planned wiring and past test reports.

## Pushed foundations before this continuation

- Generic ELF byte endpoints, grants/revoke/connectAcross, record compatibility, external provider I/O snapshot with registry mutex released during I/O; `open_usb` ABI slot returns UNSUPPORTED.
- `serial.port` pair publishes RX/TX buffers and shuttle calls into installed class ELF rather than firmware USB physical I/O. USB provider graph and I²C/VBUS ownership work remain on the implementation branch; USB controller ELF has an unresolved IDF/link dependency.
- NativeUsbClassBridge installed-class binder, USB ClassStreamSession, stream endpoints, class-ELF host tests and ordinary package installer/stage/transaction foundations exist.
- `.rte.zip` stored-format bootstrap (`PackageRteZip.h`, `PackageRteZipInstall.h`), generic `PackageCatalog.h` parser and host `scripts/pack_rte_zip.py` are committed. The bootstrap does not yet constitute the wired online/SD install path.

## Pushed during this continuation

- `d5fe7df`: `scripts/export_canonical_driver_release.py` now discovers ordinary package manifests across four kinds and exports one `.rte.zip` per package with `package-catalog.json`; rejects undeclared files, duplicate identities and nonempty release destination. Schema-1 flat names only until schema migration.
- `147a2d5`: USB stack package builder emits `.rte.zip` assets and a generic package catalog, no fixed numeric-version whitelist. Its seven build targets and legacy `usb-cdc-acm-v2` source lineage still require migration; this is not a generic four-kind builder.
- `78f237d`: USB package host test now checks generic catalog identity, ZIP CRC/contents, archived SHA-256, imports and existing ELF relocation invariants.
- `932a9f8`, `e020ec3`: `NativeUsbBridge.cpp` now opens/adopts installed class ELF sessions through `NativeUsbClassBridge`; compatibility read/write/config/control route to its token. Failed checked close quarantines without intentionally releasing the host lease.
- `9fdec9d`, `555a509`, `b538f2d`: class stream and native class bridges retain session token / bound function table / installed ELF lease on failed physical close and provide checked stop/unbind. Reject class reads/writes reporting counts beyond request bounds.

## Actual checks and limitations

- GitHub file writes/commits confirmed by connector results. No host suite or firmware compilation ran during this continuation: repository checkout could not be obtained in the local container (DNS resolution failed). No physical USB/VBUS qualification occurred. Previous handoff reported green stream and ZIP smoke checks on the earlier snapshot only; those are NOT results for these commits.
- Review found the earlier ledger prematurely claimed `NativeSerialPortBridge` EnsureInstalled/AttachPair and `StreamRuntime` granted read/write were on origin. They are NOT on origin and remain mandatory production work. Treat present code as unqualified.

## Remaining U1 (production integration, not a CI-wait checklist)

1. Wire `NativeSerialPortBridge` EnsureInstalled + AttachPair and `StreamRuntime` granted read/write, update host stubs, perform targeted stream check once checkout is available. Reconcile USB class legacy compatibility/physical shutdown and latest provider ownership.
2. Wire online discovery and SD inbox for all four kinds through pinned `package-catalog.json` + single `.rte.zip` download + `installOrdinaryFromRteZip` common transaction. Existing `NativeOnlineDriverInstall.h`, `NativeDriverManagerBridge.cpp`, app adapters and package manager still assume legacy split inputs/latest URLs. Current `release.yml` still publishes loose app and legacy driver assets, so distribution conversion is incomplete.
3. Purge package-only P-256/signed RISC-PKG/trust/security-floor production paths, retaining SHA-256/TLS/recovery and independent runtime authorization.
4. Versioned nested resource paths and all-kind per-ID install roots; safe migration from flat apps, known older driver layouts and `usb-cdc-acm-v2` to stable `usb-cdc-acm`. Bump each modified distributable manifest numeric version.
5. Installed ELF verification fast path without repeated SHA/MD5 on normal launch/inventory; add independent ZIP service with bootstrap installer working offline absent service; connect build/release catalog with exact firmware artifact custody.
6. Resolve actual integration/build source defects; do not start U2/U3/U4, merge, tag or publish.

## Next source action

Repair missing native serial pair bind/grant paths, then wire SD and pinned online ZIP intake to the existing ordinary installer. Do not describe U1 as implementation complete until these production paths and remaining gates are connected.
