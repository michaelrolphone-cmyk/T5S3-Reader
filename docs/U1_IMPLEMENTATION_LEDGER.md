# U1 implementation ledger

[PR #96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96) on `impl/u1-riscrte` is the **only** implementation PR/branch. `AGENTS.md`, [USB remediation](USB_CONTRACT_VIOLATION_REMEDIATION.md), [execution order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md), [claim-scoped USB control](U1_USB_CONTROL_SCOPE_IMPLEMENTATION.md) and [package identity](PACKAGE_IDENTITY_VERSION_POLICY.md) govern the work. Owner controls merge, tag, release, flash and hardware qualification. A committed test is not a PASS.

## September 23 continuation: installed serial pipes without app polling

The preceding direct-I/O tree was published as `4f715a4` (same tree as local
`3b498e3`). This continuation connects the existing installed-provider serial
shuttle to the real bounded stream scheduler:

- Running RX pipes poll one bounded provider chunk before pipe work; TX drains
  one chunk after pipe work, including bytes queued without an app `write`.
  Partial/zero writes retain pending bytes and scheduler demand. Full RX queues
  stop provider reads; paused/cancelled RX pipes do not request further reads.
- Serial callbacks run outside the stream mutex. RX and TX each admit only one
  in-flight operation; completion checks endpoint generation/session identity
  before clearing flags or publishing bytes. Captured owner IDs replace mutable
  global ownership in provider queue operations.
- Trusted I/O hooks now require the captured epoch and check it when acquiring
  the installed-session pin. An operation prepared for an old session cannot
  touch a replacement provider after a preflight/reconnect race. Availability
  reads the atomically published epoch rather than racing mutable session state.
- Disconnects and sticky provider errors mark affected endpoints and attached
  pipes failed; fatal TX writes do not replay or spin. Disconnect checks also
  run on scheduler turns with saturated/paused queues. Idle sessions remain
  event-driven; this is not a new periodic device-discovery loop.
- Serial TX `finish` returns AGAIN until buffered/staged bytes have been accepted
  by the provider, then publishes EOF under the same lock as its empty check.
  This establishes provider acceptance, not an electrical wire-drain guarantee.

The new host regression runs the actual scheduler, native stream bridge and
production installed-serial bridge against the fourth inventory provider. It
covers progress without direct RX/TX calls, saturation/recovery, pause/resume,
zero/partial writes, finish retries, disconnect, reconnect, stale epochs and
handles, terminal errors, and absence of provider callbacks under the mutex.
Public v1/v2 ELF ABI layouts and distributable package sources are unchanged;
no app/driver manifest version bump applies.

Observed validation: all 32 C++ programs plus the C11 ABI check in
`test/run_stream_test.sh` passed on this final source tree with
`-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer.
`ASAN_OPTIONS=detect_leaks=0` was required because LeakSanitizer cannot run
under the local ptrace environment. The scheduler regression also passed
separately before the final epoch/finish hardening, which the full run covers.
Shell syntax and `git diff --check` passed. Publication uses the existing
PR #96 branch; its commit metadata can differ from the tested local commit,
so the exact Git tree is compared before updating the branch.
The existing PR merge conflicts, generic installed-ELF stream import/publication
wiring, firmware build and hardware qualification remain outstanding; these
host callbacks do not execute a packaged ELF or qualify physical hardware.

## September 23 continuation: direct file I/O and deferred cleanup

The previous locally tested tree was published to PR #96 as `caf5779` through
the connected GitHub API after the owner continued. Its tree exactly matches
local `83085c2`; the commit ID differs because the API supplied commit metadata.
The CLI has no GitHub push credentials. PR #96 remains open and has unresolved
merge conflicts; the matching-head workflow query returned no runs.

Production changes in this continuation:

- Direct stream read/write/seek/finish now reserve the same exact in-flight
  tickets used by pipes, execute adapter callbacks outside the global stream
  mutex, and commit results under the lock. Byte transfers remain capped at
  512 bytes. Writes use copied input; reads enter caller memory only after the
  stream generation, caller and grant are revalidated.
- Grant tickets distinguish a fresh grant from one revoked while I/O was in
  progress. Revoke followed by regrant cannot authorize an older completion.
  Stale/duplicate completions cannot release a newer operation's pin. Ticket
  exhaustion fails closed instead of wrapping.
- Explicit close, owner-wide release, and deferred close after direct or pipe
  completion detach adapter cleanup under the lock and execute it outside.
  Outstanding I/O retains the exact adapter until completion; context-wide
  cleanup has fixed 12-slot storage and item/time checkpoints with real yields.
- File open, metadata inspection, failed-open cleanup and staged-file rollback
  run outside the stream lock. Open rechecks the captured execution-context ID
  before publishing a handle; a rejected CREATE_NEW rolls back only its created
  file, while rejected reads preserve existing content.
- A provider finish result of AGAIN leaves the stream open for retry; a failed
  seek preserves its prior terminal state. Prepared control operations serialize
  with reads, writes and pipes through the same stream pin.

The storage fixture now rejects any storage operation/destructor under the
stream mutex. Production-bridge tests exercise reentrant access to another
stream, concurrent-operation rejection, close/context replacement during reads,
stale opens and context-wide cleanup. Portable runtime tests cover independent
READ/WRITE rights, revoke/regrant, partial writes, finish retries, malformed
provider counts and exact once-only retirement. No exported ELF ABI changed,
and no app/driver package payload was edited.

Verification: the final unchanged runner completed with exit 0 using
`ASAN_OPTIONS=detect_leaks=0 bash test/run_stream_test.sh`: 31 C++ programs plus
the C11 ABI check, with `-Wall -Wextra -Werror` and ASan/UBSan for C++ tests.
Both new regression programs also passed separately. `bash -n` and
`git diff --check` passed. Leak checking remains unavailable under this container's
ptrace environment. An earlier exploratory run was interrupted by editing its
running shell script; the full final run above used the completed runner and
passed. No firmware/Xtensa build or hardware validation is claimed.

Remaining: this removes stream-mutex coupling; it does not add cancellation or
timeouts to synchronous HalStorage/SD calls that do not expose them. Provider
publication/import/context wiring still needs full installed-ELF boundary proof,
and protected downstream retention/purge is intentionally fail-closed. Master
reconciliation, broader U1 work, firmware/Xtensa builds and physical qualification
remain outstanding. No merge, release, tag or flash was performed.

## September 23: stream scheduling, ownership and authorization

Continuation base: `1ed82d151281b92df6f7b79856349de18f35cd1d`, PR #96 still open.
A complete checkout is available in this continuation. Historical verification
and remaining-work statements below describe earlier revisions, not this HEAD.
In particular, `f8830fe` through `eeb4a6d` already wired installed serial inventory
and sessions into the ESP32 production branch, disabled the legacy production USB
API/class selector, and added the fourth serial witness. The earlier statement
that the inventory manager is not invoked by production is superseded. Those
changes alone do not establish physical or complete end-to-end acceptance.

This continuation implements:

- Fair `pumpPrepare` rotation across external-I/O pipes, with four-operation or
  10-ms scheduler checkpoints and an actual task delay. An always-AGAIN adapter
  cannot keep the inner scheduler loop running forever or starve other pipes.
- Exact in-flight transfer tickets retaining firmware adapter state until the
  corresponding completion. Close/context exit revoke scheduling immediately
  and defer adapter destruction; duplicate/stale completions cannot mutate a
  replacement stream or unpin a newer operation. Concurrent I/O to a pinned
  adapter is refused. Provider write buffers are copied before dispatch.
- Pause/resume preserves a completed read and accounts for partial writes.
  EOF returned with a final payload drains that payload and terminates without
  repeatedly calling the source. Cancellation discards staged work without
  delivering late data into a reused pipe slot.
- Direct typed-record reads/writes and metadata honor explicit context grants,
  retaining atomic short-buffer behavior and independent READ/WRITE rights.
  The existing pipe-connect API accepts authorized granted endpoints without
  exposing publisher owner IDs. Downgrading rights fails affected live/paused
  pipes and discards their staging; each scheduling turn rechecks authority.
- Protected-source pipe copies fail closed, including into a self-declared
  protected sink. A destination flag does not establish downstream retention,
  revocation or purge authority. Direct authorized consumption remains available.
- Existing compile/fixture defects repaired: initialize the serial publication
  adapter's registry reference; bound diagnostic detail formatting; replace a
  literal escaped newline in the installed-serial fixture; link the existing
  host-only class-I/O adapter into legacy serial bridge fixtures without
  duplicating their class-binding stubs. The non-production legacy epoch adapter
  now maps epoch zero to a live nonzero semantic generation and exhaustion to
  inactive zero. No production fallback was added.

No public byte-v1/record-v2 ABI layout, app or driver payload changed. No package
version bump or release is implied. The new asynchronous lifecycle regressions
and grant/record regressions are part of the existing stream test runner.

Verification: `ASAN_OPTIONS=detect_leaks=0 bash test/run_stream_test.sh` completed
with exit 0 after the repairs: all 29 C++ test programs plus the C11 ABI check.
C++ checks use `-Wall -Wextra -Werror` and AddressSanitizer/UndefinedBehaviorSanitizer.
LeakSanitizer was disabled only because this container reports that it cannot
operate under ptrace; leak checking is not claimed. The standalone asynchronous
fixture also passed cancellation/reused-pipe coverage. `git diff --check` passed.
No firmware/Xtensa build, new matching-head GitHub CI result, or hardware result
was observed in this continuation; PlatformIO is not installed locally.

Remaining at `caf5779`: direct file I/O and cleanup still held the stream
mutex; the continuation above addresses that lock coupling. Underlying finite
operation/cancellation bounds remain to be resolved where adapters lack them. Generic installed-ELF publication/import/context wiring and downstream
protected retention/purge must be verified at the real provider-to-app boundary;
registry-level grant tests are not proof of that entire boundary. Master
reconciliation and the other U1 acceptance items remain open; no merge, release,
flash or hardware qualification occurred.

## Ancestry, versions and scope

Last measured ancestry, September 21 at `6e0a52b`: master `839f66c` had 119 master-only commits and U1 had 283 branch-only then. Last complete master backmerge `1ebfbfe` at `0a8c21c`. Master PR #103 CH34x and #105 DMA/VBUS were selectively incorporated, **not fully backmerged**. Master firmware 1.2.48 then; U1 firmware 1.2.35 remains unreleased. U1 apps: Driver Manager 1.0.5, App Store 1.0.3, Package Manager 1.0.1. Driver manifests: board-power 0.1.6, USB controller 0.1.5, host 0.1.4; CDC forked `usb-cdc-acm-v2` 0.1.2→0.1.3 (`c846628`)→**0.1.4** (`af6ba50`); CP210x 0.1.3→0.1.4 (`ad38c6f`)→**0.1.5** (`9fa6f47`); CH34x 0.1.2→0.1.3 (`d62ace1`)→**0.1.4** (`3a9c351`). CDC canonical ID migration remains unresolved; CH34x remains experimental/unpublished. No release was cut.

## Previously committed production baseline

- Generic ELF byte/record streams, execution-context-owned leases and revocation, partial I/O/backpressure; one recoverable four-kind per-ID SD/ZIP package engine with bounded bootstrap, SHA-pinned catalog, shared app/driver/package managers and manifest-driven exporter.
- Installed `i2c.bus` ELF is the ONLY permitted temporary bounded raw firmware I²C importer pending U3. Installed `board.power.vbus` ELF owns BQ25896 charger/OTG/VBUS/telemetry and BATFET shutdown with UI/sleep grants and SD/startup sequencing. BQ27220 is a separate gauge. No unverified REG12 charger-current ADC capability or physical qualification is claimed.
- `02ff428`–`fd94430`: host checked release, CDC/CP210x session and orphan retention. `ca2c26f`–`5d9b5b5`: controller staged interface/device close and detached reap. `db75070`–`6b7b41b`: selective master #105 bounded 500-ms DMA halt/flush/callback drain and IDF/VBUS quiescence and quarantine. Historical isolated sanitizer tests do not establish current-head ELF results.
- `d534030`–`f63c2f8`: independent CH34x ELF with actual WCH matching, vendor commands, line coding, DTR/RTS and bulk transfers. `5eaa653`–`9d81322`: CP210x confirmed detach vs unknown-state checked close. `9e56af0`–`9c5790e`: host legacy device-token control rejects; claim-scoped interface and sole-claim vendor controls implemented in all three class ELFs.
- Through `bcd89c1`: `efadf13` host snapshot ABI, `b308b86` bounded ELF-owned event/device snapshot with generation tokens and uncertainty distinct from detach; `23defff` firmware consumes snapshot rather than polling/listing/config-descriptor parsing and retries quarantined class/host cleanup; `b2e0c51`, `ff62bba`, `bab09c1` snapshot/teardown tests/runner; `5942bc9` host 0.1.4.

## September 21: generic publication, provider graph and ELF probes

- `f7d8a90`, `e15799c` introduce generic transport-neutral, generation-checked publisher with stale/metadata collision rejection and checked withdrawal; `6fa11df` routes the **transitional compiled USB projection** through it; `e1a685a`, `9498bcb` add tests. That earlier production projection was not class-originated.
- `352fd72`, `affff67` preserve exact private serial tokens after malformed/failed acquisition and uncertain teardown. `c1335b8`, `6e0a52b`, `da5147e` test and wire exact retry and nonpublication of partial results.
- `0504f3d`, `990884a` introduce installed-manifest/API candidate enumeration, exact grants and checked rejects with no compiled class/VID list; `8b1ae48` retains unbound class grants for cleanup; `594d63c`, `952a56e`, `5f9b959` tests/runner; `59deb28` previous ledger.
- `044bc89` retains interface-less failed release grant; `a31dd84`, `3007746` generic selected-session ownership and quarantine with `aaee82f`, `c7603d5` regression; `fa9f579`/`9eb5411` retain failed USB host grant and permit running-host reconfiguration. `3026b38`, `6473517` integrate the REAL host + three independent class ELFs against simulated physical controller (descriptor parsing, controls, partial I/O, release/reconnect), wired into `test/run_usb_host_v2_test.sh`, not observed PASS. `0d94ef9` ledger/PR.
- `64161ec`, `bb0ed5f`, `a720d6e` add exact grantless activation recovery via checked ELF unload and dependency release only after quiescence. `56b8513`, `b815c94`, `112950c` retain failing candidate ID in generic selector/session; `49bb4f8`, `3b2bc38`, `557d1d4` test isolated graph recovery while another provider retains its independent live grant. No observed PASS.
- `a50acc1` append-only class-owned probe extension; `c48fc65`, `9f76033`, `3f78b60` add read-only CDC/CP210x/CH34x descriptor/VID matching in their ELFs, no claims/vendor controls during probe and negative uncertainty fails closed. `90f7e75`, `5394b4e` connect probes to the **still compiled** transitional selection using exact host device token on every candidate. `93009f9`, `f15918a`, `b5784b4`, `b6200bf`, `710913a` add source/protocol/no-side-effect, synthetic fourth manifest-candidate tests and runner wiring. A synthetic candidate is not a fourth packaged functional class acceptance. `f5cd4ed` prior ledger.

## September 21: serial lifecycle, structured errors and CI continuation

- `6e2757d`: serial bridge reserves private cleanup token before opening physical class, preserves it if teardown after failed open is uncertain, and stops treating app serial closure/context end as an actual host detach. Device identity remains available for a different app until provider announces real disconnect. `2b4db0a` revokes a terminating context's public lease while retaining the exact private provider token for checked retry.
- `219fb9c`, `a9d4983`, `24c6b52`, `881595c`, `144173c`: registry and production serial/device C API simulated regressions for stale public lease, checked class-close failures, failed-open quarantine, provider-owned persistent discovery, reconnect and context restart.
- `061c386`: failed host activation without any issued grant retains the exact host package identity; checked stop uses targeted graph recovery, quarantining failure rather than silently activating another host or globally shutting down other services.
- `d7f1e2f`, `0909ccb`: generic registry optionally snapshots read-only structured provider diagnostics (actual result, copied provider identity, numeric error and ASCII-sanitized bounded detail before physical cleanup); app-facing serial wrapper no longer scrapes `getLastLogs()`, `USBREF`, `VBUSREF` or `USBCTRL`, nor calls `t5_usb_get_api` to obtain diagnostic text. `3157b3e`, `435b260` add regression/runner. **Physical ELF root-cause/error-stage fields still need to populate the optional callback**; no complete stage diagnostics are claimed. `8520d61` recorded earlier changes.
- `b6a10e7` wires existing real-host/class integration runner and serial-stream runner into the EXISTING automatic USB PR workflow; adds missing serial/stream source and test path filters. No extra workflow, manual approval or dispatch was created. No run was observed on `b6a10e7`.
- `ce48120`: checked RX/TX endpoint cleanup after class quiescence; a close failure retains its exact handle, private serial token and exclusive physical lease for retry. Failed opens now retain even partially allocated stream pairs, never returning unpublished handles to callers. Already-closed endpoint INVALID is treated as quiesced; other errors quarantine. `56d0300` extends production serial bridge fixture with failed TX close, partially opened endpoints, failed RX close, repeated retry and reconnection. **Tests committed, not observed passing.**
- `2c22e94`: app-visible serial acquisition diagnostic is bound to the originating execution-context identity, rejects stale failure records when an unauthorized caller retained a function pointer and prevents the next context reading the previous context's error. `94afa79`, `b649ede`, `f755d59` add, wire and fix the production-bridge context-isolation regression under `-Werror`. `c3c3bf9` last ledger. Builds/tests remain unobserved.

## September 21 continuation: installed class-originated serial inventories

- `6fe8bc7`, `af834c3`, `73a7dc4` distinguish failed verified provider inventory from genuine exhaustion in the generic selector and add negative regression; a verification fault cannot silently select another installed class. `4cc8497` updates the selected-session fixture for the checked prepare prerequisite.
- `5d5e05c` introduces transport-neutral `sdk/driver/RiscSerialPortV1.h`: unchanged `serial.port@1` function-table prefix, read-only probe, bounded generation-qualified semantic device inventory, uncertainty distinct from healthy zero devices; `4408098` reuses that ABI through the existing USB provider typedefs without compiling USB protocol into generic core.
- `32694c4`, `22932a8`, `f25e41d` implement the inventory inside the actual CP210x/CDC-ACM/CH34x class ELFs. Each asks the installed host for a bounded token snapshot, runs its own read-only probe, publishes opaque token/generation/transport only, stages all results before copying to a sufficiently sized output; host/descriptor errors fail without reporting a false detach. No interface claim, control or bulk transfer is performed by this snapshot. **This production class API is implemented but the active app path is not yet using it.**
- `aaffe1f`, `ca4317a` add and wire real production host + all three class ELFs against simulated controller for empty/full/short inventory, same-token generation, read-only discovery and unknown descriptor. `f1549ca`, `4566737`, `a10f58f` update older graph/CDC/CH34x fixtures for mandatory host poll/devices and prevent false success from mock hosts missing the new ABI. No PASS observed.
- `d72147c` implements generic generation-bound `SerialProviderDevices` from the ELF-supplied inventory to existing `RuntimeDevices::Registry`, with provider-scoped identity, exact detach/reconnect revocation, no publication on uncertain inventory, and checked withdrawal. `3c76c2a`, `fed474b` add and wire regression. `8ad8ab5` corrects explicit slot-constructor initialization (a genuine C++ compile defect found in a local isolated smoke), adds app-handle-to-exact-generation resolution; `bb43933` tests stale-handle rejection and same-generation continued use. The isolated smoke is NOT a current-head repository build.
- `6495e81`, `4582d70`: hardware-blind `InstalledSerialInventory` manager uses installed capability enumeration and exact provider grants, consumes each class-owned inventory and publishes devices, retains exact grant/ID for checked shutdown and grantless graph recovery. Partial shutdown quarantines further discovery until exact retry; no compiled USB host or class switch. `c9db447` adds a two-provider manager fault/reconnect/release fixture; `3439663` wires it into existing provider-graph runner. `f5bb491` updates source-boundary checks for the real new ABI and generic monitor, instead of asserting the now-superseded legacy class ABI text. Manager source is **not yet invoked by production application discovery or serial acquisition**.
- Class manifests updated without publishing: CDC `usb-cdc-acm-v2` 0.1.4 (`af6ba50`), CP210x 0.1.5 (`9fa6f47`), CH34x 0.1.4 (`3a9c351`), with the experimental CH34x publication status unchanged. Packaging derives version dynamically from manifests; no loose asset or release was produced.

## Historical September 21 verification and remaining-work snapshot

Historical green GitHub workflow `35427506383` ran at old `7bffec3`, NOT these commits. GitHub branch/action lookup still showed only old workflow results and no workflow run for checked new code SHA `3a9c351`; complete checkout is blocked by container DNS resolving github.com (reproduced this continuation). Isolated C/C++ struct/constructor smoke checked a limited syntax fix, **not a repository build/test PASS**. Production host/class inventory, generic monitor, serial-stream and firmware/Xtensa/app/package/catalog/import/relocation tests are all **committed or runner-wired but not observed passing**. No merge, release, tag, flash, owner hardware qualification, manually gated workflow or second implementation PR.

1. **Primary architectural blocker remains:** compiled `NativeUsbBridge`/`NativeUsbClassBridge` still coordinate selection and session binding, and `NativeSerialPortBridge_implementation.inc`/`NativeUsbDeviceRegistry`/`UsbSerialProjection` and `NativeStreamBridge` still own USB-specific device/stream shuttle paths. The installed ELFs now originate matching/inventory, and a generic grant-owning monitor/registry adapter exists, but the monitor is **not wired to normal runtime app discovery**, and apps do not yet acquire/pump/release ELF-owned `serial.port` streams through it. Wire the new generic manager as the sole semantic source, remove duplicate compiled publication and transport shuttles, and prove a genuine fourth packaged class to Serial Monitor/ESP programmer with DTR/RTS, partial I/O, backpressure and reconnect. Do not mistake a synthetic fourth candidate or separate isolated tests for full end-to-end acceptance.
2. Populate provider-owned physical root-cause codes through the generic diagnostic callback, and verify checked provider-owned streams. Current wrapper log scraping is retired, but detailed stage attribution remains incomplete.
3. Reconcile master divergence; implement exclusive USB/debug-console PHY handoff/restore and quarantine on uncertain release.
4. Safely migrate forked `usb-cdc-acm-v2` to canonical `usb-cdc-acm` and retire legacy proxy. Purge signing/P-256/floor preserving SHA, TLS, ABI/import checks and rollback; finish nested resources and generation-bound ELF verification receipts.
5. Obtain actual matching-head firmware, Xtensa ELFs, apps, imports/relocations, package/catalog, missing/corrupt install and charging/OTG/sleep test evidence. Owner performs hardware qualification only after software completion. U3 owns native I²C/SPI/UART and unrelated peripherals; U4 CAM provisioning.

**Implementation In Progress.**
