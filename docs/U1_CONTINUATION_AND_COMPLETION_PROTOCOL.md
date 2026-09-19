# U1 continue-driven implementation protocol

**Normative; September 18, 2026.** Read [root AGENTS.md](../AGENTS.md), [implementation-first workflow](IMPLEMENTATION_FIRST_QUALIFICATION_WORKFLOW.md), [U1 milestone](NEXT_HARDWARE_TEST_MILESTONE.md), [four-milestone order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md), [I²C bus ELF cutover](I2C_BOOTSTRAP_CUTOVER.md), [SPI/UART bus ELF cutover](SPI_UART_ELF_BOOTSTRAP_CUTOVER.md), [streams audit](STREAM_PIPE_MILESTONE_ALLOCATION.md) and [status reporting](U1_STATUS_REPORTING.md). Complete code blocks without intermediate owner tests or CI-wait loops. Preserve safety, identity/version, honest build status and recovery. Older demands for all-green CI/comprehensive E2E/release assets before implementation completion are superseded by the three-step workflow.

## 1. Start the implementation context

[PR #86](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/86) is a specification baseline, not implementation. Verify merge state; after owner merges, create ONE new U1 branch from current master and ONE PR targeting master. If unmerged, report precise blocker; do not merge for owner or pretend master includes it. Inspect real source/commits rather than treating specs as implemented. Keep concise `docs/U1_IMPLEMENTATION_LEDGER.md` on implementation PR for completed/remaining work, commits, versions, actual checks/blockers and next source action. Ledger is not a substitute for coding.

## 2. Before Work Complete, `continue` means implement

Every owner `continue` requests meaningful next code in the SAME PR: inspect, implement, commit and run proportionate checks, fix known blocking failures. Continue independent work while CI runs or unrelated checks fail. No repeated manual approvals, intermediate hardware tests, status-only turns, speculative redesign or brittle milestone qualification suites.

**Binding internal sequence, not new milestones:** (0) minimal linked-source inventory; (1) generic ELF-published byte/record Stream/Pipe endpoints, cross-context rights and safe lifecycle FIRST; (2) establish the installed I²C bus ELF's stable public interface with its **private** temporary firmware raw-transfer backend, then functional USB controller/host/class/power ELFs and Serial Monitor/programmer on their `serial.port` streams; (3) generic-stream online/SD ZIP intake, four-kind manager/version/catalog/source work; (4) integrated software handoff. Signing-only purge and independent repairs may run in parallel. The existing generic ABI must support later SPI/UART bus providers and bus-only private imports; **do not add SPI/UART drivers to U1 just for completeness**. If an actual U1 driver requires either bus, publish its bus ELF first, with only that ELF temporarily importing its raw firmware port. Follow [SPI/UART cutover](SPI_UART_ELF_BOOTSTRAP_CUTOVER.md). U1 A–F legacy headings do not override stream-first order. No U0.

Finish actual provider-owned USB, generic `serial.port`, non-USB witness, four-kind online/offline lifecycle, `.rte.zip` bundles/per-ID layout, ZIP bootstrap/service, safe legacy and CDC ID migration, generic catalog/release, versions, signing purge and installed-ELF performance. First-run provisioning is deferred.

### Bus ELF boundary: only bus providers may proxy raw firmware controller operations

[I²C Bus ELF Cutover](I2C_BOOTSTRAP_CUTOVER.md) requires an installed, activated I²C bus ELF publishing `i2c.bus` (or compatible versioned contract) **in U1**. That ELF alone may import the bounded temporary firmware I²C controller/transfer primitives. Power/expander/USB-supporting and other I²C device ELFs require its capability and implement their own register/protocol/power policy; reject direct firmware I²C/`Wire`/`kernel.i2c` imports. The bus ELF mediates rights, transactions and single-controller ownership; a missing ELF fails closed. Record package ID, public ABI, private importer, actual noncyclic dependency graph, safe startup/timeouts and U3 removal in implementation ledger. Native I²C controller ownership is NOT a U1 Work Complete gate.

**SPI/UART follow the same provider pattern, but their device migration is U3 unless an actual U1 dependency demands earlier use.** Generic core cannot know `spi.bus`/`uart.port` semantics; an SPI or physical UART bus ELF must publish the stable public capability before any corresponding device ELF migrates. That bus ELF alone can import an explicitly allowlisted private bounded raw SPI/UART firmware port backend; device ELFs never can. U3 swaps the bus ELF's internal backend for native controller ownership and removes normal firmware imports without a second consumer rewrite. Do not equate USB CDC `serial.port` with the ESP32 hardware UART bus or remove necessary isolated boot/ROM diagnostics. Never authorize device-specific proxy ELFs or broaden the temporary port exceptions to USB host/class logic.

### Mandatory stream integration and performance

Preserve merged byte v1/record v2/App Store/GNSS work. Complete real installed-provider-owned endpoints, cross-context permissions, bounded backpressure, revocation and quiescence; use actual USB ELF RX/TX for Serial Monitor/programmer and common bounded cancellable online/SD ZIP transfers. No blocking provider I/O under global stream mutex or unauthorized downstream pipe copies. No broad optional transforms, tee, merge, camera, display or LoRa scope.

Implement [ELF Load Verification Performance](U1_ELF_LOAD_VERIFICATION_PERFORMANCE.md): no repeated whole-file SHA/MD5 for ordinary committed launch/inventory, retain install/uncertain recovery/explicit verification/permission/ABI checks and complete generation-bound validation plus coherent fast inventory. UI actions must not synchronously rehash installed ELFs.

Use targeted ZIP/path/bounds, integrity/rollback, import authorization, version/identity, lifetime and bus-boundary tests where useful. Report failed/unrun checks. Before all code is complete, end **Implementation In Progress** or truthful blocker label.

## 3. Work Complete

When required production code is connected and no known blocking source/build defect remains, exact standalone bold final line **Work Complete**. Above it report PR/commit, checks run and omitted. Implementation completion is not physical acceptance, all-green CI, released assets or merge approval. Never use for docs-only or incomplete code; owner controls merge/tag/release/flash.

## 4. Improving Code

Later `continue` without qualification invitation means real review, improvement, targeted checks or defect repairs on SAME PR. Favor ZIP/extraction/recovery, provider lifetime, migrations, consistency, latency, diagnostics. No status-only turn. End **Improving Code**; new blocker returns to implementation until corrected.

## 5. Release Qualification

Only explicit owner direction starts physical qualification. Collaborate on hardware/integration, diagnose/fix and identify accepted revision/assets on actual evidence. Merge/tag/release/flash require separate express authorization. End **Release Qualification**; this is third step, not automatic release.

## 6. GitHub quota and access failure

Claim exhaustion only on explicit 403/429 rate limit, headers or Retry-After. Stop retries and disclose verified SHA/reset only if provided. Normal 404/409/auth/transport/5xx are not quota. Continue independent work only from reliable snapshot; verify HEAD after recovery. Never invent commit or reset times. A truthful blocker status can temporarily replace normal label.

## 7. Reporting and prohibitions

Briefly state code, checks/blockers and next task; finish with one bold standalone 2–5-word truthful status. No firmware USB/device implementation, direct firmware bus imports in peripheral ELFs, USB-only catalog, package edits without version bumps, `foo-v2` ID version dodge, split-file normal distribution, signing revival, early provisioning, fictional PASS, repeated owner tests or unauthorized merge/release. **Temporary raw I²C/SPI/UART firmware operations are allowed solely behind their installed matching bus ELF, not exposed to upstream drivers; U3 retires normal-runtime imports.**

## 8. Subsequent milestones

[U2](NEXT_MILESTONE_INDEPENDENT_PACKAGE_ECOSYSTEM.md) independently publishes packages, adds external sources and profile-aware roles after accepted U1 and owner direction; not a U1 Work Complete gate or Improving Code task. Keep established bus package IDs/public capabilities, with any private temporary backend isolated to bus ELF. [U3](NEXT_MILESTONE_T5_PRO_HARDWARE_DRIVER_MIGRATION.md) migrates fitted peripherals, establishes SPI/UART bus ELFs before dependent consumers, and replaces I²C/SPI/UART ELF internals with exclusive native controller ownership; [U4](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md) provisions and captures one real image. Reuse U1 generic streams. Advance only with owner direction and accepted baseline.
