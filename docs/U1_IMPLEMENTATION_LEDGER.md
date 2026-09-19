# U1 implementation ledger

PR: [#96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96), branch `impl/u1-riscrte`, originally based on master `52226bc` after spec PR #86. This ledger records code pushed, not inferred completion or hardware qualification.

## Foundations pushed before this continuation

- Generic ELF byte endpoints, grants/revoke/connectAcross, record compatibility and external provider I/O outside the stream mutex. The `open_usb` ABI slot is UNSUPPORTED.
- `serial.port` RX/TX pair, native stream shuttles calling installed class ELF, provider graph and I²C/VBUS ownership inventory; USB controller ELF has unresolved IDF/link integration.
- Native USB class bridge, USB ClassStreamSession, host test fixtures, ordinary manifest/preflight/stage/transaction installer.
- Stored `.rte.zip` bootstrap (`PackageRteZip.h`, `PackageRteZipInstall.h`), generic `PackageCatalog.h` and `scripts/pack_rte_zip.py` existed. These were not wired to production SD/online install.

## Pushed during this continuation

### Release package production

- `d5fe7df`: manifest-discovered four-kind ZIP release exporter with generic `package-catalog.json` and bounded source inventory; nonempty destination is preserved rather than deleted.
- `147a2d5`: seven hardware-dependent USB driver build targets now produce ZIP packages plus generic catalog and derive numeric versions from their source manifests, rather than a hardcoded version whitelist. This particular builder is still USB-specific and still uses legacy `usb-cdc-acm-v2` identity.
- `78f237d`: USB package host test adapted to generic catalog, ZIP CRC/content, archive SHA-256, imports and relocation checks.
- `c5eec42`: ZIP packer refuses unsafe/undeclared files, case-fold collisions, symlinks and packages that exceed the actual firmware bootstrap limits (17 archive entries including manifest, 1 MiB each, 4 MiB aggregate, 4096-byte manifest).

### Class ELF, lease safety and stream access

- `932a9f8`, `e020ec3`: USB compatibility bridge opens/adopts installed class ELF, shuttles read/write/config/control through class bridge, and quarantines failed close before host lease release. No new firmware physical USB implementation.
- `9fdec9d`, `555a509`, `b538f2d`, `152ab97`: USB class session/native bridge retain token, table and installed ELF lease on failed physical close, provide checked stop/unbind, reject invalid provider byte counts, and resolve installed class availability before provider selection.
- `07e46ab`: `NativeSerialPortBridge` calls `EnsureInstalled`, starts class token, publishes RX/TX and attaches the exact pair to the current execution context; failed physical stop prevents normal lease clearing.
- `09c598b`: semantic serial-provider registry keeps the same public lease retryable when its physical provider refuses teardown.
- `d62227a`: registry byte-stream read/write honor cross-context grants. Unknown/non-granted handles yield INVALID; recognized owners/grantees missing a right yield DENIED.
- `11fc77b`, `09c267d`, `29a4653`: added host regressions for byte grants/revoke, class close failure and retry, and serial public lease retry. `d92de11`, `b6d6e4a` add a host-only checked-close fake and link it into serial tests; real class-binding test continues to link the production bridge.

## Actual validation

- Connector confirmed each file write/commit on `impl/u1-riscrte`; no new PR, merge, release or hardware operation was performed.
- **Neither `test/run_stream_test.sh` nor `test/run_unified_package_mvp.sh` ran on these new commits.** The local container could not clone GitHub because DNS resolution failed. The previous agent's older green tests are not current validation. No firmware image, physical USB or VBUS test ran.
- The current PR mergeability is false against master. Reconcile without dropping work when source integration is complete; do not merge automatically.

## Remaining U1

1. Run/fix stream and package host suites and inspect actual code paths for duplicate host/class sessions, failed-close retries, context shutdown and provider grant ownership. Do not treat the committed regressions as passing until executed.
2. Replace normal online and SD four-kind intake with pinned `package-catalog.json`, one immutable `.rte.zip` per package and `installOrdinaryFromRteZip` through the existing transaction; update Driver Manager/App Store/package manager, offline SD inbox, release workflow and retained bounded legacy adapters. Current `NativeOnlineDriverInstall.h`, `NativeDriverManagerBridge.cpp`, App Store adapters and `release.yml` still use legacy paths.
3. Remove package-only P-256, signed RISC-PKG, trust-policy and device-security-floor logic from active production paths without removing ordinary SHA-256, TLS or rollback.
4. Implement nested paths/schema revision and all four per-ID installation roots; migrate known existing installs safely and normalize CDC lineage to stable `usb-cdc-acm` with proper numeric version bumps.
5. Remove repeated whole-file SHA/MD5 on normal ELF launch; resolve any source/firmware compile defects and remaining T5UsbApi serial compatibility paths. USB controller linkage remains a blocking source issue.
6. No U2/U3/U4, automatic merge, tag, release, flash, fictional CI or hardware qualification.

## Next source action

Wire existing ZIP bootstrap to the SD inbox and pinned immutable online generic catalog, preserving the ordinary installer and all unknown user files; then qualify changed host suites and repair any failing source tests.
