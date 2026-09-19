# U1 implementation ledger

PR: [#96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96), branch `impl/u1-riscrte`, originally based on master `52226bc` after spec PR #86. This ledger records pushed code, not inferred completion or hardware qualification.

## Foundations pushed before this continuation

- Generic ELF byte endpoints, grants/revoke/connectAcross, record compatibility and external provider I/O outside the stream mutex. The `open_usb` ABI slot is UNSUPPORTED.
- `serial.port` RX/TX pair, native stream shuttles calling installed class ELF, provider graph and I²C/VBUS ownership inventory; USB controller ELF has unresolved IDF/link integration.
- Native USB class bridge, USB ClassStreamSession, host test fixtures, ordinary manifest/preflight/stage/transaction installer.
- Stored `.rte.zip` bootstrap (`PackageRteZip.h`, `PackageRteZipInstall.h`), generic `PackageCatalog.h` and `scripts/pack_rte_zip.py`. The ZIP bootstrap is not yet wired to production SD/online intake.

## Pushed in the preceding continuation

### Release package production

- `d5fe7df`: manifest-discovered four-kind ZIP release exporter with generic `package-catalog.json` and bounded source inventory; preserves nonempty destination.
- `147a2d5`: USB driver builder produces ZIP packages and derives numeric versions from source manifests instead of a hardcoded whitelist; the builder is still USB-specific and retains legacy `usb-cdc-acm-v2` identity.
- `78f237d`: USB package host verifier checks generic catalog, ZIP content/CRC, archive SHA-256, imports and relocations.
- `c5eec42`: ZIP host packer refuses undeclared/unsafe files, case-fold collisions, symlinks and packages outside actual firmware ZIP limits.

### Class ELF, lease safety and stream access

- `932a9f8`, `e020ec3`, `9fdec9d`, `555a509`, `b538f2d`, `152ab97`: installed class binding and checked physical teardown retain token/ELF mapping/package pin on uncertain close; reject invalid provider byte counts.
- `07e46ab`: serial bridge acquires installed class, starts it, publishes endpoints and checks exact owner/stream attachment; a failed physical stop prevents normal lease clearing.
- `09c598b`: semantic serial-provider lease stays retryable if physical teardown fails.
- `d62227a`: registry byte read/write honor cross-context grants and distinguish absent handles from denied rights.
- `11fc77b`, `09c267d`, `29a4653`, `d92de11`, `b6d6e4a`: added host regressions/fixtures for stream grants, checked close/retry and physical serial provider lease; source compiled against real bridges but has not been rerun on these commits.

## Pushed in the current continuation

### USB production RX/TX corrections

- `9060914`: `ClassStreamSession` now retains bounded incoming and outgoing chunks across RX backpressure, partial/zero class writes, and refused physical closes. One-sided stream grants no longer invoke a zero-rights grant; failed second grant revokes the first.
- `a668506`: class-session host test covers full RX, no read-ahead, exact replay, partial/zero TX writes, one-sided grants and failed-close recovery.
- `4c34493`: app-facing `NativeStreamBridge.p2.inc` no longer drops the unaccepted RX suffix or unwritten TX suffix. It holds at most one 512-byte chunk per direction and copies session epoch before unlocking around class I/O.
- `791a34c`, `6069253`: existing stream task retries pending TX at a bounded 10 ms cadence and clears the retry flag when drained/error/closed; app reads also supply a retry opportunity. `txInFlight` serializes concurrent app/task attempts.
- `a1b02ce`: remove the premature pair attachment inside `nativeStreamOpenUsbPair`; `NativeSerialPortBridge` must first establish exclusive device ownership, then checked-attach exactly once. Before this change the double attachment could reject every otherwise-valid acquisition.

### ZIP bootstrap preflight

- `65e8709`: `installOrdinaryFromRteZip` now validates contiguous local data layout and every entry's stored ZIP CRC before ordinary staging. Checks use bounded 512-byte reads with cooperative checkpoints and heap-owned multi-kilobyte ZIP/manifest views. Ordinary SHA-256, ABI/import and transaction authority remain separate.
- `d6598d3`, `98efddc`: CRC mutation, hidden-gap, reordered topology and failed-read regression added to ordinary package host runner.

## Actual validation and limitations

- GitHub confirmed the file writes on this same branch. No new branch/PR, merge, release, flash or hardware operation was performed.
- **The revised stream and package host suites have not run** on these new commits; the container cannot clone GitHub because DNS resolution failed. No firmware build or physical USB/VBUS test was performed. New tests are assertions in source, not a PASS claim.
- `get_commit_combined_status` for `98efddc` returned no status checks. PR mergeability was previously false against master; reconcile without dropping changes after source integration.
- The current USB controller ELF remains unlinked against the required IDF host/power path. Existing `NativeUsbBridge` compatibility calls still need retirement; the class session and app-facing shuttle are two implementation helpers, not independent physical USB owners.

## Remaining U1

1. Run/fix stream and package host suites and address real source/build errors. Verify the new retry/close boundary against provider quiescence, app shutdown and USB disconnect. CI is feedback, not a gate to unrelated implementation.
2. Wire SD inbox and online four-kind intake through `installOrdinaryFromRteZip`, one pinned generic `package-catalog.json` and one archive per package. Replace normal `NativeOnlineDriverInstall.h`, `NativeDriverManagerBridge.cpp`, App Store, package-manager and `release.yml` legacy paths while preserving bounded historical adapters and unknown user files.
3. Remove package-only P-256/signed RISC-PKG/trust-policy/security-floor paths, preserving ordinary SHA-256, TLS, import authorization and rollback.
4. Implement nested resource schema and all four per-ID roots, safe legacy migration and canonical `usb-cdc-acm` identity with monotonic manifest versions.
5. Eliminate repeated installed-ELF whole-file hashing on normal launch, resolve USB controller linkage and remaining build/integration defects. No U2/U3/U4 scope.

## Next source action

Wire the existing ZIP preflight/bootstrap into SD inbox and pinned online generic catalog, retaining the ordinary transaction and offline support. Run focused host checks when a source checkout becomes available; do not claim completion before four-kind consumer/install/release integration is real.
