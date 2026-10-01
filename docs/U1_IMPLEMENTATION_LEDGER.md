# U1 implementation ledger

## Active continuation claim — October 1, transferred 03:09 UTC

Owner: resource-closure continuation on PR #96 / `impl/u1-riscrte`.
The coordinating owner verified that the prior implementation worker was
interrupted and explicitly transferred this claim at 03:09 UTC. The inherited
28-file staged resource slice in the app-bundle worktree is preserved for review;
its parent is **cf1ac054**. This is an explicit transfer, not claim expiry.
Continuation starts from verified green `4b5c1816`, integrates observed master
`a5e2db59`, and keeps sole ownership while publishing/verifying this checkpoint
and auditing the remaining whole-U1 acceptance gates. Other continuations must
not write this branch until this claim is explicitly released. No live release,
index, tag, master update or flash. CI is feedback, not an idle-work gate.

## October 1: archive attribute refusal and retained target evidence

The target stack-audit correction is published **d86f411a** (local
**8914630e**, identical tree **5eeb42172b1ce199fff043e19e70a5917cee215f**).
Matching-head workflows **36815881756 / 36815881749** were running before
this later source change; no final success is inferred.

The actual archive ELF from **68e3b8d4** workflow artifact **11141147400**
was downloaded and checked against artifact SHA-256
`f0832237d58c3eda09018389275d8db1f51bc700afc48fe501cd9d51c007f0e4`.
Its 115656-byte ELF has SHA-256
`5fd8d477c693a307c04f0ef9a37daa6d23d0ac44fb4546fab445c689a32c0bdb`.
Current actual loader preflight, exact 16-import manifest matching, structural
validation and 64-site relocation map pass. The existing generic packer/exporter
and package/catalog checks produced the 116990-byte service ZIP, SHA-256
`e9b872a1ecf6f1abd429ac2aabeb02c979c9bfaa37ff2712a9a9e583724538dc`.
This is retained older-ELF evidence, not a later-source build or hardware run.

Further source review against bundled-package requirements found that Unix
executable permission bits were still accepted. The shared bootstrap now rejects
those bits, as it already rejects links, directories and special objects;
execution roles come only from the checked manifest. A focused regression failed
before the change and passes after it. Full ordinary-package aggregate and
loaded archive-service sanitizer checks pass. Canonical packer outputs use zero
attributes, so normal package compatibility is preserved. Archive 0.1.0 remains
its initial cumulative unreleased version; final target verification is pending.

## October 1: observed archive target build correction

At **68e3b8d4**, PlatformIO/host workflow **36815185748 passed**. The
USB/ELF run **36815185806** reached the new service and actually compiled/linked
its Xtensa ELF, then failed its strict builder import check: the inherited target
flags generated `__stack_chk_guard` / `__stack_chk_fail`, absent from the service
builder's local subset. They are already scoped OS/CPU ABI-v1 exports. Preserve
stack protection and admit exactly those two symbols in the builder's subset;
do not weaken the runtime gate or accept unknown device imports. This correction
changes validation, not the produced service payload or its version. Python
syntax checks pass; matching-head target checks remain pending.

The second master backmerge published **2b885a7a**, tree
**8d14bc4ed1b60ccb9cf784d897138e523da1109a**, equal to local-tested
**d64c9cca**. Its target workflows are **36815725621 / 36815725674**;
they predate this builder correction and are not evidence for it.

## October 1: second current-master reconciliation

Owner master advanced to **1e0188c1** while archive admission repair
**a4c2c573** was being published (local **2045f568**, identical tree
**de0af2d547b624aaaad3bbcf80815221161dcdbe**). This backmerge retains the
merged headless checkpoint and X-button navigation work. It does not advance
U1 into CAM qualification or implement new board ports. Model Viewer ZIP
identity advances **1.2.5 -> 1.2.6** above the incoming loose 1.2.5 release;
source behavior and navigation driver 0.1.2 are inherited unchanged.
The two test conflicts preserve the incoming behavior assertions and a numeric
minimum 1.2.5 rather than pinning an obsolete distribution version. Model Viewer
contract/controls and all six lab guard tests pass. The full HID aggregate also passes; final-head
target workflows remain pending. Firmware version reconciliation remains open.

## October 1: archive loader admission integration

Master backmerge **68e3b8d4** matches local-tested **bd73bda5**, tree
**d547e941398cd9c6539a24406bd871f28ee57476**. Its target workflows
**36815185748 / 36815185806** started after mergeability was restored.

Independent source review found `memcmp` and `strncmp` in the shared ZIP/path
primitives. Both are already ordinary public firmware libc exports, but the
provider loader's explicit libc snapshot omitted them. A regression using the
actual preflight rejected `memcmp` before the correction. The snapshot now
includes those two existing generic libc operations; device imports and
manifest/grant checks are unchanged. Full privileged OS/CPU, exact-manifest,
resolver and relocation tests passed after the fix. CI now also applies actual
firmware preflight and exact linked-import checks to the archive service ELF.
Target observation remains pending. No distributed service source changed, so
service version remains 0.1.0; final firmware reconciliation is still required.

## October 1: current-master reconciliation

Archive service published as **d437e4cc18210e44dcd0f4eb24a57b5eac528917**;
its tree **c5caf8f535cec3f51051337131b66aa89908251f** equals host-tested
local **45f244e3**. Specific owner publication approval resolved the earlier
scope denial. Exact-head target CI is pending; no result is inferred.

This separate backmerge integrates master **f79f7291**, retaining merged
Hollow mechanics, confirmation handling and button-remap fixes. The only content
conflict was Hollow's manifest version. ZIP identities now exceed the incoming
loose distributions: three apps 1.0.1 -> 1.0.2; Hollow 1.1.37 -> 1.1.40,
reserving the independently active 1.1.39 source identity without incorporating
unmerged source. Firmware inherits 1.3.48; a final U1 version increment remains
required. Full native-app sanitizer aggregate passed after conflict resolution, including
actual confirmation and button-remap regression cases; archive loaded-service
and ten provider-discovery/package tests also passed. All 38 source app versions
exceed this master. Target ELF/package builds remain pending; the archive-only
head had no PR runs while the master conflict blocked mergeability.

## October 1: independently installable archive service

**48d84450** is the verified bounded-intake checkpoint (localb0c32d0d,
identical treeefc4f6e), green on PlatformIO/host **36811898525** and
USB/ELF **36811898523**. These workflow results certify that checkpoint,
not the later archive service changes described below.

The next slice adds **service/archive-zip0.1.0**, a software-only capability on
the existing provider graph with generic clock dependency. Copied input/output,
one128-KiB job,17 stored entries,512-byte API calls and a60-second job budget
provide bounded listing/extraction without filesystem authority. Shared bootstrap
structure/topology/CRC primitives now also accept explicitly non-package ZIPs;
ordinary installation still requires its manifest and complete SHA/transaction
validation and never needs the service. Empty ZIPs/files work. Provider-local OS mutex admission,
stopping-before-quiescence and generation tokens preserve safe lifetime/retry.

The existing source discovery, ordinary packer and catalog/exporter now include
service/provider manifests with their real kinds. Existing driver payloads and
versions stay unchanged; this new service starts at0.1.0. Host loaded-module and
common package/discovery tests passed; actual Xtensa link and package CI remains
pending. See U1_ARCHIVE_SERVICE_IMPLEMENTATION.md for the exact subset, tests,
release-tooling distinction and limitations. The legacy ZipFile implementation is unchanged; no fault-revalidation
result for that independent path is claimed here.

**Implementation In Progress**

## October 1: strict ordinary archive intake bounds

**24b84db6** is the verified four-kind resource checkpoint (local89860197,
identical tree75982438), green on PlatformIO/host **36810742575** and
USB/ELF **36810742520**. Master remains **a5e2db59**. The resource changes
introduced no app/driver payload changes, but final U1 firmware version
reconciliation is still mandatory before requesting merge/release readiness.
Check actual master and published firmware lineage then; do not repeatedly bump
for internal checkpoints or absorb an unmerged feature's version as if merged.

The next bounded slice feeds the selected catalog archive length into strict
staging before writes. The generic HTTP/file stream helper retains one chunk,
replays short/AGAIN writes, rejects excess input before SD write and enforces
both a total300-second budget and the existing no-progress timeout. The wrapper
includes network readiness and post-close file-size verification in that budget;
no authenticated stream context means failure rather than fallback to an
unbounded transport. Only an exclusively created stage may be removed on failure.
Ordinary archive SHA inspection now has a60-second elapsed limit and byte/time
yield checkpoints. Underlying blocked-call termination remains open.

Focused sanitizer tests passed for exact bytes, oversized/truncated replies,
continuous progress past deadline, late read/finish, timer wrap, stalled and short
writes, absent context, raced/existing stages and final metadata/close faults.
The actual downloader method is exercised with the production transfer/registry,
substituting only platform storage and network-readiness boundaries. Full
stream and ordinary-package aggregates, metadata freshness routing and driver
package compatibility tests passed. Exact-head target checks remain pending. No TLS changes,
archive service, installation semantics, versions or live release changed here.

**Implementation In Progress**

## October 1: provider-owned installed resource bindings

**57c4b0a3** is the published app-resource checkpoint (local4a2d772f,
identical treea7365bee). Both firmware targets/all app ZIPs passed; USB/ELF
**36810000332** passed. PlatformIO/host **36810000245** found a source assertion
still requiring the old unconditional launcher statement. Cleanup remains
unconditional after the resource admission branch. The source assertion is
updated to that branch; seven USB-package tests pass. Full app/stream aggregates
had already passed at this tree; the expanded driver aggregate now passes.

The provider/driver/service slice carries the manager-admitted identity through
executor and owned graph into each loaded module's context. An optional host-table
suffix opens four private read-only resource streams, reusing the app resource
validator, package pins and existing direct-I/O tickets. Resource handles cannot
be published as public endpoints; revoke retires adapters outside the mutex and
invalidates in-flight output. Old SDK layouts and distributable bytes are unchanged.

Focused three-kind bridge tests and an actual loaded-ELF resource call passed,
including copied identity, stale/open/read revocation and failed-quiescence retry.
Provider graph/lifetime/admission aggregate passed with ASan/UBSan and
LeakSanitizer disabled for executor ptrace limits. The initial graph runner
reset ASAN_OPTIONS and triggered that known tooling limitation; the rerun used
LSAN_OPTIONS too and passed. Full driver and stream aggregates passed.
No claim of media-remount integrity or terminated blocked SD I/O is made.

**Implementation In Progress**

## October 1: scoped installed application resource access

**e377948b** publishes the inherited declared-tree implementation and explicit
claim transfer. Its PlatformIO/host **36809347714** and USB/ELF **36809347775**
workflows passed; later code is not covered by those runs.
The full ordinary package sanitizer aggregate,21 record and19 index tests passed.

The next connected slice adds a read-only installed-resource API backed by the
existing byte-stream registry. The trusted app loader binds its verified identity;
resource handles pin the package generation, reject undeclared/executable paths,
revalidate context after SD access and retire outside the stream lock. Existing
stream ABI layouts remain unchanged. No app payload/version changes are needed
for an additive runtime API; no app was converted or republished here.

Focused production-bridge tests passed for rights, pinned replacement refusal,
metadata/length mismatch, stale invocation and uncertain close; the full stream
aggregate passed with ASan/UBSan (LeakSanitizer disabled for executor limits). Provider/driver/service resource bindings remain open.
See U1_NESTED_RESOURCE_IMPLEMENTATION.md for exact guarantees and limitations.

**Implementation In Progress**

## October 1: declared nested resource transport/install/recovery

Verified predecessor **cf1ac054** passed PlatformIO/host **36808116432** and
USB/ELF **36808116419**, including both firmware targets and all38 actual app
ZIP exports/record validations. The preceding015ae5d4 built those successfully
but exposed a stale Risc Strike test expecting loose assets; cf1ac054 fixes the
assertion and validates the ZIP contents, index insertion and retry instead.

The next coherent resource slice extends the existing ordinary engine with
schema2 nested resources while retaining schema1 flat inputs and root executable
identity. Shared declared-tree inventory serves stage/install/verify/uninstall;
unknown data is preserved, implicit parents are bounded, cleanup is deepest-first
and metadata-last. The host packer/export/record/index and runtime catalog use
the same path semantics. [Implementation evidence](U1_NESTED_RESOURCE_IMPLEMENTATION.md)
records exact bounds, fault coverage and the remaining lower-I/O limitation.

Local tests cover all four kinds, nested round trips, corruption/path attacks,
partial stage cleanup, deletion interruption/restart, unknown files/directories
and actual SD-operation read/close/create faults. Existing manifest/release/index
checks remain compatible. New target CI is pending; cf1ac054 does not certify
these new changes. No app/driver payload/version changed in this engine slice.

Scoped generation-pinned resource access is the dependent next closure; resource-
only packages and archive service remain open too. CDC migration, verification
receipts and final bounded-I/O/link evidence remain in the matrix. The single-
writer claim stays active; no interim owner hardware test is requested.

**Implementation In Progress**

## October 1: independent app ZIP distribution closure

Approved signing cleanup is published at **36fad636**; exact-head target CI is
running. App distribution work was implemented independently from green
**844a6d08**, then reconciled with that cleanup and current master **a5e2db59**.
Master's Hollow Trail changes are preserved; no master branch was written.

Normal local app release output now uses one immutable ordinary ZIP per app,
with isolated app/driver artifact catalogs and a shared offline record/index
validation gate. Runtime catalog merging supports app ZIP locators and retains
legacy numeric-version barriers and old loose/GameBoy inputs. The legacy app
adapter validates/skips ZIPs; the common package API owns their installation.
Canonical launch rejects a sidecar/ordinary version mismatch and recovers when
coherent metadata is restored.

[Distribution migration evidence](U1_APP_BUNDLE_MIGRATION.md) records the exact
published lineage, loose-to-ZIP identity rule and every old/new app version.
36 manifests receive version-only increments; App Store1.0.8 and Driver
Manager1.0.7 remain unchanged. Hollow Trail1.1.37 exceeds newly merged master
1.1.36. No app C content changed beyond that inherited master work. Existing
mandatory app requirements now propagate into ordinary bundle metadata.

Observed host checks: 20 release-record,19 index,11 release-plan,18 bulk-release,
8 GameBoy compatibility tests, release retry and workflow tests; actual ZIP to
runtime URL round-trip, managed-app identity and sanitizer-backed online refresh
recovery checks. Ordinary package suite passed. Combined native-app aggregate
is being rerun after repairing two stale exact-version test expectations.
Both board CI jobs now export/validate all real built app ZIPs; exact combined
head results remain pending, not covered by older green commits.

Remaining closures are nested resources/scoped access, archive service, CDC
migration, verification receipts and bounded-I/O/link evidence. The claim stays
active; there is no live catalog/release/tag/flash operation.

**Implementation In Progress**

## October 1: remove the isolated package-signing subsystem

Base **844a6d08** has verified successful PlatformIO/host **36803877752** and
USB/ELF **36803877914** runs. Its predecessor **75316089** passed both target
boards and USB/ELF but exposed a missing owner-tick stub in the standalone input
focus harness; 844a6d08 fixes that harness and asserts its progress cadence.
Master was refetched at this checkpoint and remains **3300229d**, already merged.

The [purge audit](U1_SIGNING_PURGE_AUDIT.md) lists every exact removed path and
retained neutral dependency. Deleted **45** isolated signed-format decoder,
P-256/trust/floor/provenance/device-wrapper, writer, fixture/runner and experimental
spec files, rather than archiving a disabled subsystem. No ordinary production
caller depended on that subgraph. Existing ordinary manager/ELF helpers already
hold the useful parsing, SHA, byte ownership, exact imports, transaction and
quiescence machinery, so no replacement installer was introduced.

- Remove unused authentication security-version fields/checks from internal
  preflight; preserve ordinary numeric package versions and downgrade refusal.
- Rename internal declared-import/content-digest/private-manager admission
  fields/methods to match the actual unsigned contract. Public driver/stream
  ABIs, exclusive trusted admission, exact import equality, privately copied
  candidate bytes, dependency pins and failed-quiescence quarantine remain.
- Rewrite active provider/security guidance around the retained controls and
  remove stale package-signing mandates. Unrelated TLS/general crypto and
  firmware-image protocol hashes remain; no credential/eFuse/device setting
  or deployed SD/NVS data was changed. Unsupported legacy containers reject
  read-only and preserve their bytes. App/driver payload versions are unchanged.
- Added a normal-suite absence/reference guard; no deleted source/tool remains
  a build/release dependency. Negative ordinary parser/ZIP checks reject retired
  metadata/container input without changing its owner-held bytes.

Observed after removal: full ordinary-package, springboard, stream, provider
map/graph and authorization suites passed, including real loaded ELF context/
quiescence, corruption, stale rights and exact import/owned-byte checks. Also
19 release-index, 16 release-record and 10 offline handoff tests passed. ASan/
UBSan used with only ptrace-incompatible LeakSanitizer disabled. Exact new-head
firmware/ELF CI is pending at publication; prior green results do not certify it.

This closes another actual U1 source gap. Remaining app bundle distribution,
nested resources/scoped access, archive service, CDC migration, generation-bound
receipts and final link/physical-ownership evidence remain in the acceptance
matrix. The older single-open SD hashing branch is already represented in current
source and is not being reimplemented. The sole-writer continuation claim remains
active while this work is verified and remaining closures are selected.

**Implementation In Progress**

## October 1: close generic installed-provider progress dependency

Built on verified **8d8f2472**: PlatformIO/host run **36801871668** and USB/ELF
run **36801871590** both passed, including both firmware/native-app boards.
The claim remains active; this is implementation convergence, not an owner
handoff. See [whole-U1 acceptance matrix](U1_ACCEPTANCE_MATRIX.md) for source-
backed completed, incomplete and verification-pending requirements.

The existing byte/record registry, loaded-ELF attachment, exact context/grants,
quiescence and unlocked direct/piped I/O were already implemented and tested.
The actual gap was generic graph progress being invoked only by serial/device
discovery, indirectly depending on GUI input or device API calls.

- Register the existing bounded graph dispatcher as a firmware-only owner-loop
  callback. Main-loop work and authenticated byte/record/pipe-progress calls
  invoke it outside the stream mutex; UI polling uses the same hook to preserve
  prior pacing. Device discovery no longer owns generic provider dispatch.
- Reentry is refused. No concurrent graph task, new stream registry, retained
  ELF callback, changed public ABI or replacement serial transport was added.
  Existing dispatcher rotation, four-callback/10-ms turn budget, 2-ms provider
  budgets, yields and failed/quarantined-module exclusion remain in force.
- Extended the actual loaded non-USB ELF fixture: producer byte/record queues
  and both sink types progress without any UI/device discovery call, including
  before a foreground app. Unauthorized calls cannot schedule work; revoke/
  uncertain quiescence still suppresses polling and retains mapping safely.
- Full local stream suite, loaded-provider context suite, USB-host suite,
  provider graph suite and the actual four-class installed-provider/pipe stack
  passed. The latter exercises witness/CDC/CP210x/CH34x, bounded physical I/O,
  backpressure and checked failed close. ASan/UBSan used; no hardware claim.
  No app/driver payload version changed. New exact-head CI is pending publication.

Next: target link/reachability and remaining compatibility/PHY evidence, then
close the named U1 package/resource/receipt/signing gaps without duplicating
working foundations or advancing other milestones.

**Implementation In Progress**

## October 1: per-package immutable online sources and version-safe coexistence

The ordinary online manager now consumes the validated independent index and
retains each selected ZIP's own immutable tag. The original four-kind aggregate
catalog remains a compatible source, without rewriting its schema or treating
catalog metadata as activation/import authority.

- Added a bounded, duplicate-aware portable index parser for current driver ZIP
  records and historical app/driver records. Exact outer identity/version/CPU,
  ordinary manifest, canonical archive/tag/own-repository URL, size and digest
  must agree. Unknown formats, mixed metadata, corruption and partial JSON fail
  closed. Historical GameBoy source/tag semantics remain supported.
- Merge by numeric `(kind,id)` version. Newer valid ZIP wins across sources;
  equal ZIPs require identical payload metadata and prefer the independent tag.
  Historical loose entries are version barriers: equal/older aggregate ZIPs are
  suppressed, never interpreted using the loose ELF hash. App/service/provider
  aggregate rows retain their own valid paths; CPU filtering remains explicit.
- A required independent-source failure clears all selectable rows rather than
  falling back to stale aggregate versions. A wholly unavailable aggregate is
  optional, but received-invalid, oversized or partial transfers fail. Publish
  the combined snapshot only after complete validation; reject union overflow.
- Fixed PSRAM allocation covers response and catalog arrays, not just payload
  text. Intake caps index at 512 KiB, aggregate at 32 KiB, 128 legacy apps,
  64 drivers and 64 selected ZIPs; parser/network checkpoints share a 90-second
  deadline and real scheduler yields. Allocation failure is tested at every
  object/response stage. Reused manifest scratch and in-place reset avoid large
  nested stack temporaries; host compiler frame inspection confirmed reduction,
  not a measurement of target stack high-water.
- Independent tags use 128-byte buffers. Existing archive/Inbox validators now
  support valid publisher names without truncation and agree with the existing
  ZIP inspector's 4 MiB + 65536 byte envelope. Download SHA, selected manifest
  identity, common transaction/recovery, active-use and installed-version gates
  remain unchanged. Public package-manager ABI and app/driver payload versions
  are unchanged; merged Hollow Trail changes/versions are retained.

Observed checks: full ordinary-package host aggregate (including two new C++
parser/merge programs), real packer→record→index→runtime URL round-trip, actual
production refresh/selection harness with simulated I/O under ASan/UBSan,
19 index tests, 16 record tests, eight driver package tests and catalog freshness
passed. The full native app aggregate passed again after final metadata allocation/stack
refinement; exact new-head firmware/ELF CI will be recorded on PR #96. LeakSanitizer was disabled solely for executor ptrace incompatibility.
The runtime parser also accepted the fetched historical index's 40 app/21 driver
records and mapped all 22 real prior target-built ZIP records to exact immutable
URLs. These checks perform no release or live index operation.

### Remaining implementation and immediate priority

This closes driver ZIP online locator integration, not all U1 acceptance.
Independent ordinary-app publication and consumption still need a coherent
migration: loose app entries remain barriers/compatibility inputs, and can
suppress old aggregate candidates. Nested resources/scoped access, generation-
bound verification receipts, canonical CDC migration, signing-only purge and
remaining normal-runtime USB extraction are still open. Existing underlying
HTTP/TLS and archive-transfer limitations are not certified by catalog parsing.

The next work is a code-backed matrix against the whole U1 definition, with
installed-ELF generic byte/record producer/sink endpoints, context/generation/
rights, bounded backpressure, revoke/quiescence and provider I/O outside global
locks checked first. Reconcile independently advanced source before adding
replacement implementations. Preserve the daily-use serial/flashing/clock/USB
workflows; no touch/network/U2–U4 scope expansion or hardware success claim.

**Implementation In Progress**

## October 1: coherent independent release workflow and artifact custody

Checkpoint on sole PR #96 / `impl/u1-riscrte`, integrating master
**`be82695e`** and preserving its latest Hollow Trail changes. Claim released
at publication; next continuation checks PR head/checks and active writer first.
No workflow dispatch, live release/index update, tag, master merge or flash.

The inspected workflow contained a partial merge: independent job headings with
obsolete aggregate build/publication bodies, undefined `steps.ver` references,
missing plan handoffs and no coherent final independent publisher. Restored the
current master independent-job structure and connected it to U1's validated ZIP
record/build contracts instead of trying to mutate every consumer at once.

- Every selected firmware/app/driver job downloads the run plan, builds only
  that product and validates its exact artifacts before upload. Driver handoff
  includes exported ZIPs/catalog only, not hidden intermediate manifests.
- A new bounded offline `verify_release_plan.py` gate checks whole-plan types,
  identities, duplicate/count limits, source versions, exact artifact hashes,
  symlink/size bounds, app sidecar integrity and firmware OTA alias identity.
  Restored artifacts are rechecked before invoking the existing publisher.
- A selected product must have a successful build result. Merely skipped,
  cancelled or failed selected jobs cannot open the publication gate. Removed
  the stray direct tagging/aggregate publication steps; existing GitHub token
  permissions and source-trigger scope are unchanged. No new credentials.
- Added generic aggregate `package-catalog.json` to mutable-catalog freshness
  handling. It now gets unique refresh requests/revalidation in both native and
  firmware transports; versioned catalog/ZIP URLs remain untouched.

Observed checks: **10 offline plan/handoff fault tests**, **16 record tests**,
**19 index tests**, **16 bulk-release tests**, **8 GameBoy tests**, one mocked
publication-retry test, workflow graph/undefined-step/output/failure-gate checks,
YAML parse, syntax and whitespace checks all passed. Catalog freshness passed its
actual C++ transport harness. The new offline handoff gate also verified all
**22 real provider ZIPs** from prior green `3ff5691a` artifact `11132629703`.
Tests performed no real publication. Exact new-head firmware/ELF CI is pending at
publication and will be recorded on the PR when terminal.

### Remaining before nested resources

The next complete slice is online routing: `NativeOnlineOrdinaryCatalog` still
pins one aggregate release tag for every row. Independent ZIP records need a
per-package immutable release locator and version-safe coexistence with valid
aggregate/legacy app/driver entries. No partial endpoint switch was made.
Independent app distribution still uses its preserved historical ELF/JSON path;
it needs its own coherent ordinary-bundle consumer/publication migration.
Only after these connected contracts are addressed should nested resources be
layered onto the package engine. Other U1 blockers remain as recorded below.

**Implementation In Progress**

## October 1: bundled driver release records and historical index compatibility

Checkpoint on sole `impl/u1-riscrte` / PR #96, integrating master
**`524e2cb3`** (Hollow Trail half-resolution background/native foreground)
without changing its package versions. Claim released at this checkpoint;
check the PR head/checks and active writer before the next continuation.
No live release index, tag, release, merge to master or hardware was changed.

- `build_release_record.py` now builds current driver records from the actual
  exported `.rte.zip` plus generic `package-catalog.json`, never the retired
  USB-only catalog or loose ELF. It verifies source identity/version/CPU,
  dependencies and provider ABI against the exact bundled ordinary manifest.
- ZIP intake is bounded before reading members: stored-only, 17 total entries,
  1 MiB per payload, 4 MiB content, canonical flat names and no symlink/output
  indirection. Every payload size/SHA and CRC is checked. A private validated
  snapshot is repacked with the normal packer and must match the original bytes,
  rejecting malformed topology, extra bytes and unsupported ZIP features.
  Duplicate/escaped JSON aliases and undeclared entries fail closed.
- Records carry explicit `format: rte.zip`, architecture and ordinary manifest;
  the outer digest/size identify the whole archive. Generic catalog identity,
  archive name, hash and size must match exactly. Missing intermediate stage
  files do not matter: GitHub's artifact upload omitted hidden `.package.json`
  in the observed previous artifact, but the ZIP correctly contains it.
- `update_release_index.py` validates explicit current ZIP records separately
  from historical unmarked four-file/loose-ELF records. The index remains schema
  1. Same-version byte/metadata/format changes and downgrades remain rejected;
  a historical-to-ZIP conversion requires a newer package version. Existing
  app, firmware and the allowlisted external GameBoy contracts remain intact.
- The ordinary manifest validator shares runtime identity, executable, path,
  type, capability, dependency and bounds rules with record construction.
  These integrity/locator checks grant no import or hardware authorization.
  The product-record round-trip/fault suite is now in normal host CI.

### Observed checks and compatibility evidence

Passed: **16 product-record tests**, **19 index tests**, **16 bulk-release
checks**, **9 provider-discovery checks**, **8 GameBoy sync checks**, the mocked
publication retry test, workflow-trigger check, metadata-only Python smoke,
syntax and whitespace checks. The CLI round-trip writes only a temporary local
index; publication tests mock every external write.

Read-only validation of the fetched actual release index: all **40 app and
21 driver records** validate and replay unchanged, and its firmware validates.
All **22 real target-built driver ZIPs** from prior green head `3ff5691a`,
GitHub run `36795841668`, artifact `11132629703`, produced valid new records
and a local round-tripped index. Downloaded artifact SHA-256:
`bac63ed82cc1b2dd374dd6d57d4bfe098786c02c6515498a5a30579fb209cefb`.
Those are real prior target artifacts tested with the new host scripts, not a
claim of a new local Xtensa build. Exact new-head GitHub checks are pending
at publication and will be reported on the PR without rewriting this history.

### Still unfinished

This repairs driver record/index contracts, not all release/UI integration.
The independent app path still uses its historical ELF/JSON contract, and
`NativeOnlineOrdinaryCatalog` still loads generic `package-catalog.json` from
an aggregate release rather than consuming independent ZIP index entries.
The release workflow also retains historical/combined paths that need a
separate coherent source audit before any actual release. No end-to-end live
publication or on-device online discovery result is claimed.

Nested ordinary-package resources/scoped access, generation-bound ELF
verification receipts, remaining USB/core compatibility extraction, CDC
identity migration and signing-only purge remain U1 implementation work.

**Implementation In Progress**

## October 1: master integration, sole power owner and selective package builds

Continuation checkpoint on the sole `impl/u1-riscrte` / PR #96. Integrated
master **`3c2fb008`** (including its preceding `2a55d0a0`) into U1. This is
not a merge into master. No release, tag, firmware publication or flash.
The implementation claim is released at this checkpoint; subsequent work must
first inspect the PR's exact-head CI and confirm no active writer before editing.

### Completed code

- Preserved master's external-VBUS host callbacks, Hollow Trail/native-app,
  touch/I2C deadlines, UI, release and other merged work. Combined the published
  power monitor/external-host ABI prefix with U1's charger suffix; compile-time
  assertions cover every retained callback offset. Typography leases retain
  their software-only cleanup, while installed capability cleanup retains an
  exact failed grant and navigation ownership until checked release succeeds.
- Restored the installed BQ25896 owner as the board's charger, telemetry and
  shutdown path. An older backmerge had reintroduced a competing resident BQ
  implementation despite the existing cutover guard. Current GT911 electrical
  bootstrap, PCA synchronization and backlight restoration remain intact.
- The provider classifies incoming power without mistaking OTG transitions for
  a charger. The board keeps the prior observation on a failed provider read,
  polls at most once per second and retries failed startup configuration at
  most once per 30 seconds. Clock-wrap and failure throttling are exercised
  against the actual polling function body.
- The board bridge preserves failed admission/release grants for one exact
  cleanup retry per subsequent operation. Failed cleanup prevents new mapping
  or stale-interface use. Accepted or uncertain BATFET shutdown stays pinned
  without retry, because the boolean shutdown API cannot prove safe recovery.
- Fixed both verified previous-head CI defects: Driver Manager icon regression
  now checks actual download/update/installed/blocked behavior instead of a
  local variable name; MSC's optional-extension fixture uses designated
  initialization so newly appended callbacks remain null under `-Werror`.
- Selective release planning discovers validated source manifests/builders,
  including newer packages, rather than a fixed package allowlist. The package
  builder validates metadata before side effects and actually filters `--ids`
  before linking/staging/exporting. Default all-package behavior remains.
  Metadata-only planning no longer imports the optional ELF parser eagerly.
  Driver asset selection uses the exact manifest ID/version/architecture ZIP,
  refusing stale versions, symlinks and neighboring/obsolete loose assets.

### Versions and compatibility

- `board-power-t5s3-v2`: **0.1.8 -> 0.1.9** (master was 0.1.6).
- `usb-controller-esp32s3`: **0.1.19 -> 0.1.20**. Master already published
  `driver-usb-controller-esp32s3-v0.1.19`; U1's checked teardown/snapshot changes
  cannot reuse that released version with different bytes.
- App Store remains **1.0.8** and Driver Manager remains **1.0.7**.
- The published master monitor/external-host prefix remains compatible.
  **Old unpublished U1 charger consumers are not binary-compatible with the
  repaired suffix: rebuild firmware and board-power 0.1.9 together.** New
  firmware rejects undersized older providers. No universal append-only claim.

### Observed development checks

Host driver/package, stream, springboard, native-app, idle-power and USB
host/CDC/CP210x/CH34x/FTDI/STLink/MSP/programmer/MSC/HID aggregates passed.
I2C, GT911, platform-clock, privileged-import/snapshot and VBUS-chain checks
passed. The final board aggregate passed charger, snapshot, shutdown, cutover,
poll-throttling and nine actual-source grant-recovery scenarios. ABI layout
assertions and `git diff --check` passed. Release regression: **16 tests**;
manifest discovery/selective packaging: **9 tests**, including real selected-only
ZIP/catalog output and preserved default-all behavior.

ASan/UBSan ran with leak detection disabled because LeakSanitizer cannot run
under local ptrace. These are host/emulated checks, not physical hardware.
Matching-head GitHub firmware/Xtensa results remain pending at publication;
read the PR checks/result update rather than treating historical green builds
as evidence for this tree. No local target firmware build was claimed.

### Next substantial implementation

First finish the remaining release-record/index migration: discovery and
selective build are repaired, but `build_release_record.py` still consumes
retired `usb-provider-catalog.json` / loose ELF inputs and the index expects
legacy driver assets. The new ZIP asset selector alone is **not end-to-end
publication readiness**. Migrate those record/index contracts with compatibility
checks; no release action is authorized or performed by this checkpoint.

Then schema-2 nested ordinary-package resources: preserve schema-1 compatibility,
add canonical bounded relative paths and ancestor-collision rejection, share
bounded SD inventory/parent creation/selective cleanup across stage/ZIP/lifecycle,
and extend the deterministic packer without weakening rollback or deleting
unknown data. Scoped resource access, generation-bound verification receipts,
complete generic runtime/USB compatibility removal, canonical CDC migration
and signing-only purge remain unfinished U1 work. CDC and signing changes were
not implemented in this checkpoint.

**Implementation In Progress**

## September 27: witness acquire maps endpoint exhaustion to UNSUPPORTED

`usb_witness_provider_graph_test` filled the 12-slot compiled buffer table
and expected `T5_SERIAL_DENIED` from shuttle-pair open. `installedAcquirePort`
no longer opens that pair. When the provider cannot publish distinct RX/TX
endpoints, acquire returns `T5_SERIAL_UNSUPPORTED` and leaves no lease or
handles. Attach-grant failure remains `T5_SERIAL_DENIED`. A successful
acquire publishes the stream epoch (`nativeSerialProviderActive`) and does
not open the compiled shuttle (`nativeStreamSerialIsBusy` stays false).

lilygo-epd47-s3 and t5s3-pro firmware jobs passed on `95cb973`. Host parser
and build-experimental still failed this witness assertion.

[PR #96](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/96) on `impl/u1-riscrte` is the **only** implementation PR/branch. `AGENTS.md`, [USB remediation](USB_CONTRACT_VIOLATION_REMEDIATION.md), [execution order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md), [claim-scoped USB control](U1_USB_CONTROL_SCOPE_IMPLEMENTATION.md) and [package identity](PACKAGE_IDENTITY_VERSION_POLICY.md) govern the work. Owner controls merge, tag, release, flash and hardware qualification. A committed test is not a PASS.

## September 27: App Store header stays on SD inbox until refresh succeeds

Firmware native-ELF validation on `f711e0b9` failed
`test/native_apps/app_store_release_transition_source_test.py` because header
tap still used `view = view == RELEASES ? SD_INBOX : RELEASES;` and switched
into Releases before a successful catalog refresh.

The header path now leaves Releases immediately, enters Releases only after
`refresh_releases(manager, ui)`, and reports `Release refresh failed; SD
packages available` without leaving the inbox. The source-guard matches U1's
package-manager refresh arguments rather than master's app-catalog signature.
ZIP inbox / version 1.0.8 are unchanged.

## September 27: source-guard matches endpoints-only acquire

Host parser and build-experimental both failed at `f711e0b9` on
`test/drivers/usb_dynamic_selection_source_test.py`: the production
`installedAcquirePort` slice no longer calls `nativeStreamOpenSerialPair(`,
but the source-guard still required that compiled shuttle step.

The guard now requires `hasEndpoints` / `endpoints` / `attachEndpoint` and
the fail-closed `T5_SERIAL_UNSUPPORTED` / `T5_SERIAL_DENIED` results, and
forbids `nativeStreamOpenSerialPair(` inside the installed `#if` slice.
`nativeStreamOpenSerialPair` remains a compatibility symbol in stream p3
and `usbAcquirePort`. Local rerun of the source-guard prints PASS. No
production serial path change in this follow-up.

## September 27: endpoints-only serial.port acquire on merged master `9f687c35`

PR head `bffb0f57` already parents U1 `332753ce` and master `9f687c35` (PSRAM catalogs, model-viewer, boot reveal, firmware 1.3.21). Merge-base equals current master; GitHub reports the PR mergeable. This follow-up does not reopen U2–U4.

`installedAcquirePort` now requires a provider-published `risc_serial_port_streams_v1` pair. Core bumps the stream epoch, then attaches those exact endpoints. Missing, identical, or unpublished endpoints return `T5_SERIAL_UNSUPPORTED`. A failed `attachEndpoint` grant returns `T5_SERIAL_DENIED`. The production installed path no longer calls `nativeStreamOpenSerialPair`. An advertised-endpoint failure still never falls back to raw provider read/write. `usbAcquirePort` remains the compatibility `usb.serial` transport and still opens a compiled pair; T5UsbApi stays a compatibility symbol, not the installed data plane. `serial.port` remains the sole InstalledSerialInventory consumer.

The installed-serial fixture advertises `streams_v1`, supplies endpoints, and grants `attachStream` when both endpoint and rights are nonzero. The host USB-controller drain source guard now matches master's quiesce event pump (`usb_host_lib_handle_events(1, &flags)`, `NO_CLIENTS`, `ALL_FREE`, free-all before uninstall) instead of the retired zero-timeout `finalFlags` snippet that broke CI after the backmerge.

Verification: `ASAN_OPTIONS=detect_leaks=0 bash test/run_stream_test.sh` exit 0; drain source-guard unittest exit 0. No firmware/Xtensa or hardware result in this continuation. Canonical CDC identity migration and signing purge remain deferred.

## September 27: backmerge master `b2e2ece3` (1.3.21)

Merged current master into `impl/u1-riscrte` at `32d6f104` so U1 is not
orphaned from PSRAM catalogs, layered boot reveal, GT911 package extraction,
HID text-input, mass-storage and scheduled bug fixes. Fifteen content conflicts
were resolved without dropping U1 generic ZIP/catalog discovery or stream grants.

Kept U1: generic `package-catalog.json` / `.rte.zip` exporters, Driver Manager
as recovery-only over T5PackageManagerApi, App Store/Driver Manager ZIP inbox
and grant-aware stream registry. Took master: extracted GT911 board methods,
text-input generation reservation (`UINT32_MAX`), navigation retry-before-ready,
mass-storage workflow path, firmware 1.3.21. Combined navigation quarantine
assertions with the extra master retry acquisition. Package row icons from
master now decorate U1 generic package previews.

Existing IDs bumped above both parents: App Store 1.0.8, Driver Manager 1.0.7,
usb-controller-esp32s3 0.1.19. This does not complete resident-shuttle removal,
CDC identity migration or signing purge.

## September 24: repair CI package checks after integration

At `98405e6`, both firmware targets, native applications, physical USB controller,
I2C ELF, provider ELFs and USB/stream lifecycle tests passed in GitHub Actions.
The two workflow failures were package checks: legacy catalog assertions survived
the generic package-manager cutover, and the installed-stack baseline incorrectly
used `usb-cdc-acm` while this branch still ships `usb-cdc-acm-v2` pending the
separately tracked identity migration. Corrected the baseline to the actual
manifest without changing or pretending to complete that migration.

Updated cleanup checks to read the included stream implementation and verify
exact-grant quarantine/recovery rather than obsolete global graph shutdown.
Preserved manifest parsing, integrity, installation/rollback and scoped recovery
checks. Fixed the recovery-count source guard's return type. Package verification
now prints a traceback and missing baseline IDs instead of an empty assertion.

Validation: the actual 15 CI-built package archives passed CRC/SHA, import,
relocation/MMIO, dependency and version checks; generic release export passed.
Driver checks passed through package tests after correction, as did USB package,
integrity and generic driver-bridge guards. No driver/app payload changed.

## September 23: repair conflicts with the newer master

The previous backmerge included `6569b81`, but the claim that the PR was current
was not verified against the remote mergeability result. PR #96 was still
conflicted against newer master. This follow-up integrates `8f51db1` and the
subsequent master firmware-asset commit `14b35df` into the same U1 branch.
The post-push check then detected master `ea3804f`; its documentation-only
specification-link conflict is also resolved, preserving all three links.

Preserves the reusable BQ25896 driver and separately installed board profile,
input-power monitoring, USB role switching and firmware navigation from master.
Retains U1 charger/shutdown ownership, stream endpoints, generic package discovery
and exact failed-release grants. The charger API now extends the published power
monitor prefix; a layout assertion guards callback offsets. Navigation cleanup
retains a failed grant and retries it before graph shutdown. Conventional builder
entrypoints let generic package discovery find both new power packages.

Existing package IDs: board-power-t5s3-v2 0.1.7 -> 0.1.8 and
usb-controller-esp32s3 0.1.15 -> 0.1.16, above both merged parent versions.

Local CDC aggregate, power/charger/shutdown, HID/XInput/navigation and provider
manifest discovery checks passed. Both power-provider Xtensa ELF builds passed.
Host-side tests cover simulated devices, not physical hardware. Full firmware and
physical-controller target builds remain unverified; the previously rejected
PlatformIO external collector request has not been retried or bypassed.

## September 23: backmerge current master into the U1 branch

Merged master `6569b81d3abe6c7ea44e71eed150bb4324d5909d` (275 master-only
commits) into U1 starting at `0be4f69`. Resolved 27 conflicted files. Future U1
continuations check master and periodically integrate new commits, as requested
by the owner; this does not authorize merging PR #96 into master.

- Preserved master's HID/XInput providers, interrupt receive handling, controller
  startup/PHY sequence, diagnostics, lazy dependency admission, UI/app changes,
  firmware version 1.2.65 and existing released assets. U1 retains generic ZIP
  packaging/catalog discovery, stream contexts, serial endpoints, coherent host
  snapshots and exact checked physical/session teardown.
- Resolved actual descriptor collisions: published driver diagnostics precede
  the U1 stream-binding suffix; published USB interrupt/diagnostic offsets precede
  U1 checked-release/control/snapshot slots. Compile-time layout checks cover
  these relationships. The unpublished U1 stream providers are rebuilt/versioned.
- Lazy graph preparation no longer preloads unrelated ELFs. Serial candidate
  discovery uses a bounded metadata-only snapshot; executable admission stays
  in exact named acquisition. Enumeration faults remain distinct from exhaustion.
- Combined master's fault-tolerant controller cleanup with U1's retained staged
  interface/device-close transaction. Closing interfaces reject further bulk
  and interrupt work. Published HID report-descriptor/SET_PROTOCOL requests keep
  their legacy interface-claim checks; arbitrary controls use exact-claim API.
- Changed package versions: CDC 0.1.6, CP210x 0.1.7, CH34x 0.1.6, simulated serial
  test class 0.1.3, USB host 0.1.5, controller 0.1.15, board power 0.1.7, all under
  existing IDs and above both parent versions.

Observed local validation: CDC aggregate suite (including host/class, provider
lifecycle, all four installed stream/pipe paths, startup/PHY and teardown checks),
HID/XInput tests, board-power tests, stream suite and manifest-discovery tests
passed. Host stream/stack regressions use ASan/UBSan with leak checking disabled
under ptrace. Normal Xtensa build scripts passed for CDC, CP210x, CH34x, simulated
serial test class, host, board power, HID and XInput. No physical result is claimed.

The full firmware build did not complete. PlatformIO waited while obtaining
SdFat, and automatic approval review rejected an external collector request
because it could send project/environment metadata. That request was not retried
or bypassed. No full firmware or physical-controller target-build result is
claimed for this merge. Remaining U1 work below still applies, except master
conflicts resolved by this merge and features explicitly superseded above.

## September 23 continuation: CDC, CP210x and CH34x own stream endpoints

Starting at `b3bdb4b`, all three existing USB serial class ELFs now bind the
loader's stream host, advertise RX/TX endpoints and use generic cooperative
provider polling. Installed serial acquisition attaches their queues directly;
these versions no longer activate the resident serial shuttle.

- Shared private `Drivers/common/SerialStreamPump.inc` implements bounded
  staging, saturation, partial/zero writes, endpoint allocation rollback,
  sticky I/O errors, authoritative detach and stopping data before checked
  physical close. Raw compatibility calls cannot bypass active queue ordering.
  The simulated serial test driver uses the same helper; it models no hardware
  assumed present on the owner's device.
- Each turn visits at most four session slots, services one session and issues
  at most one 512-byte read and write with one-millisecond timeouts. The generic
  dispatcher supplies elapsed checkpoints and scheduler cooperation. Staging
  remains in fixed ELF storage, outside temporary descriptor/open structs, to
  avoid increasing those embedded stack frames by kilobytes.
- CDC/CP210x session sequences now refuse exhaustion instead of wrapping to an
  old token. Existing CDC staged claim release and CP210x UART-disable/detach
  cleanup remain checked and retryable. App byte-v1/record-v2 layouts are unchanged.
- Same-ID versions: `usb-cdc-acm-v2` **0.1.4 -> 0.1.5**,
  `usb-cp210x-v2` **0.1.5 -> 0.1.6**, `usb-ch34x-v2` **0.1.4 -> 0.1.5**,
  `usb-serial-witness` **0.1.1 -> 0.1.2**. Fetched master and local release tag
  `v1.2.34` contain CDC/CP210x 0.1.0; CH34x and simulated test class are later
  unpublished additions. Canonical CDC identity migration remains outstanding.

Validation: the dynamic stack runner passes the actual installed serial bridge,
registry, scheduler and production host/class ELFs against emulated controllers
for all four classes. It checks allocation failure without fallback, controls,
RX/TX pipes, saturation, partial/zero writes, fatal errors, failed-close retry,
stale handles, detach and reconnect. The C/C++ stack builds use ASan/UBSan and
strict warnings, with LeakSanitizer disabled under ptrace. The CDC aggregate
suite includes descriptor, module, host/class, inventory, provider graph, stack,
three-ELF and teardown checks. CP210x and CH34x protocol suites also passed.
Older fixtures were repaired to supply required discovery/stream binding,
recognize extended descriptor sizes and validate CH34x register payloads in
wIndex. The stale teardown source guard now checks delegated exact-lease cleanup
and production inventory withdrawal, rather than a removed field expression.

All four normal driver build scripts produced Xtensa ELFs with their existing
export/import and ELF checks; generated manifests match source ID/version and
actual payload size/SHA-256. No `.rte.zip`, firmware or hardware result is claimed.
The shared helper is included in the existing USB workflow path triggers.
Remaining work: remove the older-prefix resident shuttle, generalize owner-loop
dispatch, reconcile master conflicts, canonical CDC migration and the other U1
blockers below. No release, merge, tag or flash occurred.

## September 23 continuation: packaged witness owns the serial data path

Starting from published `34bcb65`, this continuation adds an optional generic
provider poll suffix, owner-task dispatch with rotating fairness and item/time
checkpoints, and a packaged endpoint consumer of that mechanism:

- `usb-serial-witness` version **0.1.0 -> 0.1.1**, same package ID. The package is
  marked unpublished test-only and absent from the fetched current master tree.
  Its source now binds the stream host table, publishes bounded RX/TX endpoints,
  owns partial-transfer staging, and pumps at most one read/write per turn with
  one-millisecond I/O timeouts. No hardware identity or protocol is added to core.
- The serial capability's optional endpoint suffix allows the native acquisition
  path to grant provider-owned handles directly. Advertised endpoint failure
  fails closed; legacy raw callbacks are not used as a fallback. Other serial
  packages retain their existing path until converted.
- Confirmed detach revokes witness endpoints before further queued data work.
  Closing stops queue work before checked physical release, retaining a failed
  claim for retry. A trusted inventory shutdown refuses active/quarantined apps
  or sessions and withdraws device publication before releasing module pins.
- The dynamic stack regression uses the actual host ELF, packaged witness ELF,
  installed serial/session bridge, generic registry and scheduler, with only
  the physical controller and RTOS/storage emulated. It covers RX/TX pipes with
  no direct app serial reads/writes, saturation, partial/zero writes, controls,
  endpoint-allocation failure without fallback, error, close retry and reopen.
  The generic provider regression also checks that revoked modules are not polled.

Observed validation on the final source tree:

- `test/run_stream_test.sh`: all 33 C++ programs, C11 ABI check and two C
  shared-provider fixtures passed with strict warnings and ASan/UBSan.
- `test/run_usb_provider_stack_v2_test.sh`: both dynamic stack tests passed;
  all C provider/controller/host builds and C++ runners now use ASan/UBSan.
  The witness path also passed confirmed detach/reconnect and immediate stale
  endpoint rejection. This uses host-built versions of the package sources,
  not a built Xtensa archive or physical controller.
- `test/run_provider_graph_v2_test.sh`: all 13 reported checks passed. The old
  source guard was updated to recognize the append-only endpoint capability;
  its no-physical-claim/control inventory checks remain in place.
- Shell syntax, manifest JSON, workflow YAML and `git diff --check` passed.
  LeakSanitizer remained disabled because of the local ptrace environment.

This is not a firmware/Xtensa build or physical hardware result. Remaining work includes CDC/CP210x/CH34x conversion,
complete removal of the resident shuttle, generalizing the owner-loop dispatch
entrypoint, master conflict reconciliation and the previously listed U1 blockers.

## September 23 continuation: loader-owned provider stream contexts

The preceding serial scheduler tree was published as `7eb5a7e`. This continuation
connects the generic stream registry to mapped ABI-v2 providers:

- Optional `risc_driver_streams_v2` descriptor suffix binds a C host table before
  start without changing existing descriptors or app byte-v1/record-v2 layouts.
  The actual installed graph supplies the native factory. No new ELF import or
  hardware-specific callback is introduced.
- Provider contexts share the execution-context identity allocator and the same
  stream registry, mutex and scheduler. Their lifetime is independent of the
  foreground app. Copied byte/record operations are direction-checked, bounded,
  backpressured, and limited to four published endpoints per provider.
- Trusted capability attachment verifies the exact live lease and authenticated
  app context. Each stream grant is lease-bound; release recomputes any rights
  retained through another lease. App teardown removes bindings. Handles alone
  confer no access. Source reads and destination writes honor pipe exclusivity,
  including provider-side ingestion/drain operations.
- Unload and failed start revoke queues before quiescence. Failed quiescence
  keeps the ELF, dependency graph and revoked table mapped for checked retry.
  New generations cannot reuse old context authority or endpoint handles.
- The new C shared-object fixture runs through the real module loader, graph,
  native bridge and scheduler, including provider-to-app byte and record pipes.
  It is part of the normal stream suite; no new workflow or manual approval gate
  is created. Details and limits: [provider binding](PROVIDER_STREAM_BINDING.md).

Observed validation: the final stream suite passed 33 C++ programs, its C11 ABI
check, and the two C shared-provider builds with strict warnings and ASan/UBSan.
The new dynamic-provider test passed actual byte and record pipes, lease release
with another live lease, app-context replacement, record read atomicity, queue
quotas, revoked/quarantined contexts, reload and repeated failed starts.
The existing provider graph suite passed all 13 reported checks. Its first run
stopped because an internal ASAN_OPTIONS override re-enabled LeakSanitizer under
ptrace; rerunning with `LSAN_OPTIONS=detect_leaks=0` passed. Leak checking is not
claimed. Shell syntax, workflow YAML parsing and `git diff --check` passed.
No firmware/Xtensa result is inferred from these host results.

No distributable provider/app source changed; no manifest version bump applies. Packaged serial providers still
need to adopt this descriptor and expose stream handles through their capability
contract before the resident serial shuttle can be removed. Master conflicts,
firmware/Xtensa builds and physical hardware qualification remain outstanding.

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
