# U1 acceptance reconciliation — October 1, 2026

Authority: `NEXT_HARDWARE_TEST_MILESTONE.md`,
`FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md`,
`STREAM_PIPE_MILESTONE_ALLOCATION.md`, bundled-package and ELF-verification
specifications. This is a source-backed implementation inventory, not a new
milestone, a release request or a claim of owner hardware qualification.

Baseline: sole PR #96, `impl/u1-riscrte`, verified stream code **844a6d08**, integrating master **a5e2db59**;
signing removal is published at **36fad636** and this revision connects independent app ZIP distribution. The earlier **6b7ddc67** was a claim-only commit; its checks do not
validate later code. Both firmware/host and USB/ELF workflows passed for **844a6d08** after the
isolated focus-harness repair. The combined revision has the local checks below; its exact-head target CI is pending.
Local stream aggregate, ordinary-package aggregate and native-app aggregate
passed; ASan/UBSan ran with LeakSanitizer disabled for executor ptrace limits.
The prior full target checkpoint **4b5c1816** passed both boards and provider CI.

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
| Production serial.port uses installed class-owned RX/TX endpoints | Complete connected path; target/hardware verification pending | `installedAcquirePort` requires `InstalledSerialSession::endpoints` and exact `attachEndpoint` grants, with no raw-I/O fallback. CDC/CP210x/CH34x and witness use `Drivers/common/SerialStreamPump.inc`. Stream/USB integration runners cover partial I/O, checked cleanup and reconnect | Confirm final target/link path and consumer progress after owner-loop fix; preserve Serial Monitor/flasher DTR/RTS/control and failed-release recovery |
| No normal firmware USB behavior/fallback; fourth compatible class without core edits | Production cutover established; final link evidence pending | ESP production preprocessing excludes `usbAcquirePort`, `UsbSerialProjection` and legacy class selection from the serial bridge; `NativeUsbBridge` exposes a null-returning ABI stub and `open_usb` is unsupported. Actual four-class installed-provider tests pass. Compiled semantic shuttle helpers have no production pair-opening caller found | Confirm target symbol/call reachability and isolate any proven retained normal path; preserve host compatibility ABI/tests. Do not resurrect old ledger allegations merely from filenames |
| Stable I²C bus ELF and one BQ25896 charger/VBUS owner | Complete connected code; hardware pending | `RiscI2cBusV1.h`, restricted firmware bus ABI, I²C provider, BQ provider and `NativeBoardPowerPort`; prior checkpoint restored BoardT5S3 callers to installed owner and preserved BQ27220 gauge. Bounded retry/telemetry, grants and VBUS tests pass | Final current-head target/absence checks. Native I²C-controller takeover and unrelated fitted peripherals remain U3 |
| Exclusive console/USB-host PHY handoff and rollback | Verification pending | Controller `HostStartup.h`, `PhyRoute.h`, `RoleSwitch.h` retain capture/restore, drain and uncertain-cleanup state; prior target CI passed | Check complete linked console/host ownership and restore failure paths; no physical success claim |
| Generic SPI/UART provider/import/lifetime foundation | Generic foundation present; no demonstrated extra U1 dependency | Generic provider graph, dependency ABI and byte/record lifecycle are reusable. The specific SPI/UART cutover spec requires concrete public contracts when needed for compatibility; no current U1 physical SPI/UART dependency was identified | Preserve generic graph/import restrictions. Do not invent unused SDK tables or bus ELFs as a U1 gate; concrete fitted-client contracts and controller migration remain U3 unless an actual dependency is found |
| Four-kind common offline/online install/update/inventory/uninstall and rollback | Partly complete | Common ordinary stage/transaction/recovery/use-gate and PackageManager API; actual four-kind host tests; online driver ZIP source now joins aggregate four-kind rows through same installer | Independent app ZIP output/consumer wiring is connected with host coverage; real target bundle validation is pending CI. Preserve legacy ELF/JSON input; verify final four-view recovery together |
| Generic manifest-driven export/index with immutable per-package locators | Connected for app/driver ZIP plus aggregate contracts; target verification pending | `build_release_record.py`, `update_release_index.py`, offline release-plan gate, `PackageIndependentCatalog.h`, `PackageOnlineCatalog.h`; current source record/URL round-trip, real 22-ZIP records and historical 40-app/21-driver index validated | Apps now build/validate immutable ZIPs in isolated artifacts; legacy loose input remains accepted. See U1_APP_BUNDLE_MIGRATION.md for exact published version lineage and identity rules. No live index/release operation |
| Bounded shared-stream ZIP intake and complete resource trees | Incomplete | Online archive bytes use existing HTTP stream path and whole-archive SHA; common offline bootstrap checks ZIP topology/CRC/declared entries and installs per-ID generations | Nested declared resources/scoped installed-root access remain absent. Audit/enforce total transfer and I/O termination bounds: a no-progress timer is not an overall deadline or maximum received length |
| Independently installable ZIP service, bootstrap without that service | Incomplete | Bounded bootstrap reader/packer exist and ordinary installation does not need a service; no `archive.zip` service/capability implementation found | Reuse the bootstrap primitive in a scoped, bounded service; do not create another installer or an unrelated mandatory kind-witness service |
| Stable CDC identity and safe legacy migration | Incomplete | Current CDC manifest remains `usb-cdc-acm-v2`; legacy app/driver source and directory recovery adapters exist | Canonical `usb-cdc-acm` lineage/version migration with journal/rollback and preservation of unknown data; do not rename to evade versioning |
| Generation-bound installed verification receipts / inventory snapshot | Incomplete | Metadata-only installed inspection exists (`inspectInstalledOrdinarySdDirectory`, `inspectInstalledAppPair`); install/recovery retain SHA. No durable generation-bound receipt implementation found | Define and implement invalidation/media/recovery policy and coherent inventory snapshot; measure bounded hot-path work, preserve full install/update/explicit verification |
| Physically remove package-signing-only subsystem | Complete connected source; target verification pending | This revision deletes 45 isolated implementation/tool/fixture/experiment files, removes unused preflight security floors and neutralizes internal signing names. [Exact audit](U1_SIGNING_PURGE_AUDIT.md) records all removals and retained helpers. Ordinary-package, springboard, provider graph, authorization and stream suites pass; the new absence guard passes | Verify the final target build with the files physically absent. Preserve all SHA/ABI/import/TLS/authorization/quiescence behavior and user media; no deployed data/settings are changed |
| Final software integration and single owner qualification sheet | Incomplete | One U1 PR; current master integrated; repeated exact-head host/target checkpoints available | Close actual rows above, run final relevant builds/link/source checks, supply one coherent artifact inventory and hardware procedure only at the final handoff; no new interim owner test gate |

## Immediate closure order

1. Preserve the verified owner-progress closure (**844a6d08** target CI). Local complete
   stream aggregate and actual four-class installed-provider/pipe integration
   passed, including the extended loaded-ELF fixture. Preserve the existing
   queues, rights, generation, quiescence and direct-I/O ticket machinery.
2. Finish the USB reachability/compatibility audit against the target build and
   current independently advanced code; change only a demonstrated residual.
3. Close package app-bundle/resource/service/migration and receipt/signing gaps
   in connected slices, keeping the same branch, functionality and recoverability.
   U1 does not authorize advancing touch/network/peripheral migration or U2–U4.

The work must converge on these acceptance outcomes, not the number of commits
or checks. Code already present on master is retained rather than reimplemented.
Hardware remains owner-controlled Release Qualification.

**Implementation In Progress**
