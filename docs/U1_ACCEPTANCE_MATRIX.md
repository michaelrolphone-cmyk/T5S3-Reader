# U1 acceptance reconciliation — October 1, 2026

Current continuation after this inventory: PR #96 now backmerges actual master
**fa517fea**, including display foundation #220 and display safety #343.
The candidate reconciles Model Viewer ZIP 1.2.8, Risc Strike ZIP 1.0.4,
display provider ZIP 0.1.4 and firmware 1.3.52 above master's respective
loose/source versions. The changed-package-source PR guard checks numeric
version increases against the actual base, including all loose-to-ZIP apps;
the existing index updater checks published same-version content conflicts.
See the ledger for observed local checks. Exact-head target CI is pending;
the earlier baseline and results below are historical, not current-head proof.

Authority: `NEXT_HARDWARE_TEST_MILESTONE.md`,
`FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md`,
`STREAM_PIPE_MILESTONE_ALLOCATION.md`, bundled-package and ELF-verification
specifications. This is a source-backed implementation inventory, not a new
milestone, a release request or a claim of owner hardware qualification.

Baseline: sole PR #96, `impl/u1-riscrte`, current master **4530c8b2** integrated locally after green **1a5706e2**; merged Model Viewer and lab host checks passed.
Full native-app/archive checks also passed at the preceding f79f7291 backmerge.
Archive service checkpoint **014fb6f7** passed PlatformIO/host **36816333359**
and USB/ELF **36816333240**, including actual service ZIP/import checks.
The stream checkpoint **844a6d08**, signing purge **36fad636**, app ZIP closure
**cf1ac054**, and nested-resource checkpoint **e377948b** each passed exact-head
PlatformIO/host and USB/ELF workflows. The latest nested-tree runs are
**36809347714** and **36809347775**. Subsequent scoped application resource
access and provider resources also passed exact-head target checks at014fb6f7
and subsequent5466d93c. CDC checkpoint5466d93c passed workflows
36819882405 / 36819882484. ASan/UBSan runs disable LeakSanitizer for executor limits.

**Complete** below means that specific software requirement has connected code
and relevant observed checks. It does not imply that the whole row's hardware,
or U1 as a whole, has been qualified. **Verification pending** means existing
code needs the named production/link evidence, rather than a replacement design.

| Acceptance requirement | State | Current evidence | Exact remaining closure |
|---|---|---|---|
| One generic byte-v1/record-v2 registry, bounded queues and atomic records | Complete | `StreamRuntime.{h,cpp}`, `RecordQueue.h`; stream aggregate includes ABI, byte/record, schema, short-buffer, backpressure and pipe tests | Preserve ABI and existing tests during remaining work; no new stream registry |
| Installed-ELF producer/sink attachment, cross-context rights and generation ownership | Complete for existing endpoint contract | `RiscStreamProviderV1.h`, `NativeStreamBridge.p4.inc`, `ProviderModuleV2.cpp`, `GraphV2::grantStream`; `run_provider_stream_context_test.sh` actually loads a non-USB shared ELF and exercises byte/record queues, independent consumer grants, context restart and stale handles | Retain independent capability authorization; fixture success is not proof of every physical module |
| Revoke/quiescence, in-flight tickets and no provider/file I/O under global stream mutex | Complete for inspected dispatch paths | `pumpPrepare/pumpComplete`, `DirectIo`, `NativeStreamBridge.p1.inc::schedule/executeDirect`, deferred adapter retirement; `direct_io_test`, `direct_io_bridge_test`, `async_pipe_test`, loaded-provider quarantine test pass | Underlying adapter latency/termination bounds remain a separate open row below; no force-unmapping on uncertain quiescence |
| Generic provider progress independent of GUI/serial discovery | Complete | This revision registers the existing graph dispatcher with `nativeProviderOwnerTick`, called from the main owner loop and authenticated byte/record/pipe-progress APIs before the stream lock. Input polling uses the same hook for compatibility; discovery no longer owns dispatch. Loaded non-USB ELF fixture produces bytes/records and consumes both sink types without UI/device calls; reentry, unauthorized caller and quarantine checks pass | Verified in **844a6d08** target/host CI; retain serialized owner-task lifecycle and existing four-callback/10-ms dispatcher, without a concurrent graph worker |
| Production serial.port uses installed class-owned RX/TX endpoints | Connected path and selected target references verified; hardware pending | `installedAcquirePort` requires `InstalledSerialSession::endpoints` and exact `attachEndpoint` grants, with no raw-I/O fallback. CDC/CP210x/CH34x and witness use `Drivers/common/SerialStreamPump.inc`. Stream/USB integration runners cover partial I/O, checked cleanup and reconnect | Both c80bdee1 targets confirm API/provider-table references through installed inventory resolve/endpoint attachment (U1_TARGET_REFERENCE_EVIDENCE.md); complete indirect/PHY evidence remains; preserve Serial Monitor/flasher DTR/RTS/control and failed-release recovery |
| No normal firmware USB behavior/fallback; fourth compatible class without core edits | Production cutover and selected target entrypoints verified | ESP production preprocessing excludes `usbAcquirePort`, `UsbSerialProjection` and legacy class selection from the serial bridge; `NativeUsbBridge` exposes a null-returning ABI stub and `open_usb` is unsupported. Actual four-class installed-provider tests pass. Compiled semantic shuttle helpers have no production pair-opening caller found | Both c80bdee1 linked targets confirm null/unsupported compatibility entrypoints and the selected API-to-installed-provider table/call chain; full indirect/PHY reachability remains open (U1_TARGET_REFERENCE_EVIDENCE.md); preserve host compatibility ABI/tests. Do not resurrect old ledger allegations merely from filenames |
| Stable I²C bus ELF and one BQ25896 charger/VBUS owner | Complete connected code; hardware pending | `RiscI2cBusV1.h`, restricted firmware bus ABI, I²C provider, BQ provider and `NativeBoardPowerPort`; prior checkpoint restored BoardT5S3 callers to installed owner and preserved BQ27220 gauge. Bounded retry/telemetry, grants and VBUS tests pass | Final current-head target/absence checks. Native I²C-controller takeover and unrelated fitted peripherals remain U3 |
| Exclusive console/USB-host PHY handoff and rollback | Production cleanup faults and selected linked evidence verified at1a5706e2 | Controller `HostStartup.h`, `PhyRoute.h`, `RoleSwitch.h` retain capture/restore, drain and uncertain-cleanup state; prior target CI passed | Actual quiesce_host failure/retry fixture preserves route until DMA/host/PHY/VBUS are released and source-off observed; startup/role fixtures retained. Both exact-head workflows and compact controller references verified at1a5706e2; physical PHY success remains unclaimed |
| Generic SPI/UART provider/import/lifetime foundation | Generic foundation present; no demonstrated extra U1 dependency | Generic provider graph, dependency ABI and byte/record lifecycle are reusable. The specific SPI/UART cutover spec requires concrete public contracts when needed for compatibility; no current U1 physical SPI/UART dependency was identified | Preserve generic graph/import restrictions. Do not invent unused SDK tables or bus ELFs as a U1 gate; concrete fitted-client contracts and controller migration remain U3 unless an actual dependency is found |
| Four-kind common offline/online install/update/inventory/uninstall and rollback | Partly complete | Common ordinary stage/transaction/recovery/use-gate and PackageManager API; actual four-kind host tests; online driver ZIP source now joins aggregate four-kind rows through same installer | Independent app ZIP output/consumer wiring is connected with host coverage; all38 app ZIPs passed both target boards at cf1ac054 and subsequent green014fb6f7/5466d93c. Preserve legacy ELF/JSON input; verify final four-view recovery together |
| Generic manifest-driven export/index with immutable per-package locators | All four kinds connected and target verified at bd9b09a7 | `build_release_record.py`, `update_release_index.py`, offline release-plan gate, `PackageIndependentCatalog.h`, `PackageOnlineCatalog.h`; current source record/URL round-trip, real 22-ZIP records and historical 40-app/21-driver index validated | Both target boards at **cf1ac054** built/validated all38 immutable app ZIPs in isolated artifacts; legacy loose input remains accepted. See U1_APP_BUNDLE_MIGRATION.md for exact published version lineage and identity rules. No live index/release operation |
| Bounded shared-stream ZIP intake and complete resource trees | Incomplete | Online archive bytes use existing HTTP stream path and whole-archive SHA; common offline bootstrap checks ZIP topology/CRC/declared entries and installs per-ID generations | Earlier directory-intake and declared-tree fixtures passed at green014fb6f7/5466d93c, but actual SD ZIP staging remained flat-only through bd9b09a7. This slice connects that shared offline/online adapter to the same tree primitives; its actual production-stage regression fails on bd9b09a7 and passes locally after correction. Both target workflows36835619954 / 36835620195 passed the correction at c80bdee1 (U1_NESTED_RESOURCE_IMPLEMENTATION.md). Scoped application and provider/driver/service resources are connected through the same stream registry and generation pins, with real bridge/loaded-ELF coverage. The same target checkpoints include these bridges; Explicit schema3 non-executing service packs and authorized indexed app/provider consumers now pass local integration tests; both target workflows passed at c80bdee1 (U1_RESOURCE_ONLY_IMPLEMENTATION.md). Strict ordinary ZIP intake now supplies the catalog byte cap before writes and a total deadline, with focused production-helper/adapter coverage. Archive SHA inspection has byte/time yield and duration bounds. Target builds passed at014fb6f7/5466d93c; lower SD/network-call termination remains open |
| Independently installable ZIP service, bootstrap without that service | Connected; target/package checks passed at 014fb6f7 | `Services/archive_zip` supplies `archive.zip@1` through the existing provider graph and shared stored-ZIP bootstrap primitives. Actual loaded-ELF host tests cover copied scoped input/output, listing/extraction, empty files, expiry and teardown; ordinary install remains independent | Preserve the verified Xtensa service ZIP/catalog and explicit 128-KiB/17-entry/stored subset. See U1_ARCHIVE_SERVICE_IMPLEMENTATION.md. Independent service/provider delivery passed both workflows36829998475 / 36829998394 at bd9b09a7, including the actual built archive record/runtime locator. No live publication implied |
| Stable CDC identity and safe legacy migration | Connected; target/package checks passed at5466d93c | `usb-cdc-acm` 0.1.8 replaces alias 0.1.7 through the existing ordinary engine, shared canonical ABI-1 adapter, two-root leases and fingerprint-bound retirement/recovery. Production proxy retired to test fixtures; builders/index update enforce canonical lineage | Both workflows36819882405/36819882484 passed matching-head firmware/ELF/ZIP/catalog; reconcile final version identities; preserve unknown data and partial-recovery evidence. See U1_CDC_IDENTITY_MIGRATION.md. No live release/index mutation or physical power-cut claim |
| Generation-bound installed verification receipts / inventory snapshot | Connected; explicit-boundary and built-set host-work evidence verified at730d0773 | Metadata-only installed inspection exists (`inspectInstalledOrdinarySdDirectory`, `inspectInstalledAppPair`); install/recovery retain SHA. Observed RAM storage epochs and strict retained Package Manager inventory passed both workflows at8dafce33; staged receipts/provider snapshots passed both workflows atc33cb044 after the guard-preserving frame correction; canonical app admission passed both workflows atad7a0431 | See U1_STORAGE_GENERATION_IMPLEMENTATION.md: Discarded-close uncertainty is verified at8dafce33; compatible capability metadata reuse passed both workflows at029a58da. Receipt/provider admission is target-verified atc33cb044 (U1_VERIFICATION_RECEIPT_IMPLEMENTATION.md); canonical app admission is target-verified; graph-owned remap proof passed at a928d444; checksum-bearing loose legacy admission passed both workflows at3ba73f92; measure bounded hot-path work (U1_ELF_HOT_PATH_AUDIT.md), explicit full-check invalidation passed both workflows at730d0773; built-set host work reports are verified, preserve full install/update/verification |
| Physically remove package-signing-only subsystem | Complete at cf1ac054 | The signing checkpoint deletes 45 isolated implementation/tool/fixture/experiment files, removes unused preflight security floors and neutralizes internal signing names. [Exact audit](U1_SIGNING_PURGE_AUDIT.md) records all removals and retained helpers. Ordinary-package, springboard, provider graph, authorization and stream suites pass; the new absence guard passes | Both workflows passed with the files physically absent. Preserve all SHA/ABI/import/TLS/authorization/quiescence behavior and user media; no deployed data/settings are changed |
| Final software integration and single owner qualification sheet | Incomplete | One U1 PR; current master integrated; repeated exact-head host/target checkpoints available | Close actual rows above, reconcile the final firmware version above actual master/published lineage, run final relevant builds/link/source checks, supply one coherent artifact inventory and hardware procedure only at the final handoff; no new interim owner test gate |

## Remaining implementation versus evidence

Checksum-bearing loose app admission is connected and passed actual-parser/owned-
buffer and aggregate host tests plus both workflows at3ba73f92
(36848254092 / 36848254100). Canonical app admission passed both workflows at ad7a0431 and private
graph-owned remap evidence passed both at a928d444 (36845426266 / 36845426285).
Explicit full-verification invalidation is now connected at canonical/pair
boundaries using the existing observed generation; the actual failed-check,
in-flight and warm-recovery regression passed locally and both target workflows
at730d0773. That earlier checkpoint did not implement a lower-I/O fault policy.
The separately approved current policy now connects bounded module-store SPI
failure to permanent unavailable state and ownership-preserving manual reboot.
The loader slice passed both workflows at73288020 (U1_LOADER_IO_BOUNDS.md),
bounding VFS descriptor contention and chunked read work. The HTTP worker
correction (U1_HTTP_WORKER_TERMINATION.md) passed both workflows at64d19d64,
including the actual-dependency body-loop witness. The local single-read
legacy sidecar correction passed actual-source faults and both target workflows
at730d0773 (U1_LEGACY_SIDECAR_SNAPSHOT.md). The owner approved the SD/SPI retained-fault policy. Its connected port, task,
file, LoRa and lifecycle implementation now passes local fault tests; exact-head
target results are pending. See U1_SD_SPI_REBOOT_POLICY.md. No automatic reset
or controller-recovery policy is inferred.

Resource-only software now supports the minimum explicit data-only service use
case and real app/provider read consumers, with both target workflows green at
c80bdee1.
No broader font/theme activation or arbitrary package reads are implied.

Selected source/target call and table evidence for generic serial, disabled
legacy USB entrypoints and controller cleanup is verified at1a5706e2, with
actual callback/cleanup host fixtures. Complete electrical and arbitrary dynamic
runtime traces remain physical qualification evidence; old broader inventory
wording is not a new implementation task. The current SD policy's target checks
and final integrated artifact accounting remain to finish this code candidate.

The confirmed production ELF packing defect is corrected:
see U1_ELF_SECTION_LAYOUT_CORRECTION.md. The real clock/archive mapped-data
fixture and both target workflows passed at28282e30. Independent four-kind
distribution passed matching-head workflows36829998475 / 36829998394 at bd9b09a7;
see U1_FOUR_KIND_INDEPENDENT_DISTRIBUTION.md.

## Immediate closure order

1. Integrated candidate1a5706e2 is target-green in both workflows36861584009 /
   36861584052; compiled checkoutdf009536, exact tree30bc8336 matches tested
   local28d34d94. Masterd4df4609, Timecard1.0.3, Hollow1.1.42 and firmware1.3.50
   are reconciled. Retained evidence was published; five current compact reports
   were downloaded and hash/source checked. The approved SD fault slice below
   supersedes that earlier budgeted stopping point.
2. The SD/SPI owner decision is now approved and implemented locally. Complete
   the same candidate's exact-head target checks and fix any source/build
   regressions. Retained-fault semantics preserve the original task/TCB, mutex,
   buffers/handles and mapped code; no reset/unlock/unmap escapes uncertainty.
   Text Editor342 is integrated with ZIP0.2.3. See U1_SD_SPI_REBOOT_POLICY.md.

3. Remaining evidence: physical PHY/cable and arbitrary indirect runtime paths,
   final owner qualification (the isolated composite CAM witness now passes). The
   blocked independent receipt review is still incomplete. Pinned DNS/socket/
   TLS source has finite lower mechanisms, while arbitrary SDK interruption is
   not certified. Recheck versions/master before a later final handoff; no
   U2–U4, hardware port, release or deployment expansion.

The work must converge on these acceptance outcomes, not the number of commits
or checks. Code already present on master is retained rather than reimplemented.
Hardware remains owner-controlled Release Qualification. The isolated CAM witness
reported package installation followed by clock relocation failure of unknown
cause; it does not qualify this checkpoint or prove a shared-source defect.

**Implementation In Progress**
