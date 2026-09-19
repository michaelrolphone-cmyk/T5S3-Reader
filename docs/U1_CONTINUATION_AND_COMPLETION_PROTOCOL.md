# U1 continue-driven implementation protocol

**Normative; September 18, 2026.** Read [root AGENTS.md](../AGENTS.md), [implementation-first three-step workflow](IMPLEMENTATION_FIRST_QUALIFICATION_WORKFLOW.md), [U1 milestone](NEXT_HARDWARE_TEST_MILESTONE.md), [four-milestone stream-first order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md), [I²C bus ELF bootstrap/cutover](I2C_BOOTSTRAP_CUTOVER.md), [stream source audit/allocation](STREAM_PIPE_MILESTONE_ALLOCATION.md) and [U1 status reporting](U1_STATUS_REPORTING.md). Complete meaningful code blocks without intermediate owner hardware qualification or CI-wait loops. Older demands for all-green CI, comprehensive milestone E2E tests or release artifacts before implementation completion do not govern the workflow. Maintain safety, identity/version correctness, truthful build status and transactional recovery.

## 1. Start the implementation context

[PR #86](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/86) is a **specification-only baseline**, not the implementation branch. Verify whether it has merged; after owner merges it, create ONE new U1 branch from current master and ONE PR targeting master, no stacking. If still unmerged, report that precise blocker without pretending master contains the specs or merging for the owner. Read current source instead of assuming a spec is implemented. Maintain a concise `docs/U1_IMPLEMENTATION_LEDGER.md` on the implementation PR with actual completed/remaining work, consequential commits, package version bumps, checks/blockers and next source action. Update it at meaningful checkpoints, not in lieu of coding.

## 2. Before Work Complete, `continue` means implement

Every owner `continue` requests a substantive next source task on the SAME PR: inspect real state, change code, commit coherent work, run proportionate checks and fix known blocking failures. Continue independent work while CI runs or unrelated checks fail; no repeated manual approvals, intermediate hardware tests, status-only messages, speculative redesign or brittle comprehensive qualification suite.

**Binding internal order (not additional milestones):** (0) minimal inventory; (1) complete generic ELF-published byte/record Stream/Pipe endpoints, cross-context rights and safe lifecycle FIRST; (2) establish installed I²C bus ELF's *stable public provider interface* with its private temporary firmware transfer backend, then integrate the fully hardware-owning USB controller/host/class/power ELFs and migrate Serial Monitor/programmer to published `serial.port` streams; (3) use those generic streams for online/SD ZIP ingress and finish common four-kind manager, version, catalog and sources; (4) integrate and hand over one software candidate. Signing-only purge and independent fixes may proceed in parallel. Source-specific U1 workstreams A–F do not override this owner-selected sequence; no U0.

Complete provider-owned USB behavior, actual generic `serial.port`, non-USB functional witness, four-kind online/offline package lifecycle, `.rte.zip` bundles, per-kind/ID installation directories, ZIP bootstrap/service, safe legacy and CDC identity migrations, generic catalog/release integration, version enforcement and unwanted signing-only purge. First-run SD provisioning remains deferred.

### The only temporary I²C exception

Follow [I²C Bus ELF Bootstrap and Cutover](I2C_BOOTSTRAP_CUTOVER.md) literally. **The I²C bus ELF must be an installable, activated provider during U1 and must publish its versioned `i2c.bus` (or existing equivalent) capability. It alone may import the bounded, versioned temporary firmware I²C port primitives.** Power/expander and every other I²C device ELF must declare a dependency on this provider capability and perform its own device register/protocol/power policy. They MUST NOT directly import or call firmware I²C, `Wire` or `kernel.i2c`; reject such imports, including any new USB-specific shortcut. The bus ELF mediates all client transactions; temporary firmware functions implement only raw controller setup/transfers, with finite timeouts, restricted imports, one physical controller owner and no peripheral semantics. Document actual bus ELF package ID, ABI, dependency graph, power-safe startup, allowed private imports and U3 removal obligation in the ledger. On missing/disabled bus ELF, dependent capabilities fail closed; firmware must not stand in.

**U1 Work Complete requires the stable I²C bus ELF boundary and its functioning dependent provider chain; it does not require that bus ELF already own raw controller implementation.** U3 changes the bus ELF's *private backend* to native ELF controller ownership, removes its firmware imports and adapter, and proves existing power/expander consumers continue using the unchanged public bus ABI. The bus ELF is the sole permitted temporary proxy; peripheral ELFs forwarding device behavior into firmware remain prohibited.

### Mandatory stream integration and performance

Preserve merged byte v1, record v2, App Store and GNSS work. Complete a generic installed-provider-owned endpoint, cross-context permissions/ownership and bounded backpressure, revocation and quiescence; use genuine USB ELF RX/TX for Serial Monitor/programmer, and generic bounded cancellable online/SD ZIP transfer. No global stream mutex spanning potentially blocking provider I/O, no unauthorized downstream pipe copies. This is not permission to implement every transform, tee, merge, camera, display or LoRa feature now.

Implement [U1 ELF Load Verification Performance](U1_ELF_LOAD_VERIFICATION_PERFORMANCE.md): stop repeated whole-ELF SHA/MD5 on ordinary committed-generation launch/inventory hot paths without skipping install, uncertain recovery, explicit verification or permission/ABI checks; finish safe generation-bound validation and coherent fast inventory before U1 Work Complete. Routine UI actions must not synchronously rehash installed executables.

Use focused ZIP/path/bounds, integrity/rollback, capability/import authorization, version/identity, provider-lifetime and bus-boundary tests as needed. State failed and unrun checks honestly. Do not ask the owner to qualify incomplete software. Before required code is all implemented, end with **Implementation In Progress**, or an accurate 2–5-word blocker label.

## 3. First step: Work Complete

When every required production component is connected and no known blocking source/build defect remains, give the exact standalone bold final line **Work Complete**. Above it state PR/commit, checks actually run and checks not run. This announces implementation completion only, not owner hardware acceptance, all-green CI, released assets or merge approval. Never use it for docs-only work or missing required features. Owner alone controls merge/tag/release/flash.

## 4. Second step: Improving Code

A subsequent owner `continue`, without explicit qualification request, requires meaningful independent review, improvements, focused testing or defect fixing on the SAME PR. Audit package extraction/recovery, provider lifetime, migration, consistency, latency and diagnostics as useful; no status-only answer or unauthorized scope. End with **Improving Code**. If a new blocking defect invalidates completion, return to implementation and fix before re-announcing Work Complete.

## 5. Third step: Release Qualification

Only an explicit owner request starts qualification. Collaborate on real hardware/integration tests, diagnose and patch failures, determine accepted revision/assets and qualify a release on actual evidence and owner acceptance. Merge/tag/release/flash still require express owner authorization. End with **Release Qualification** while in this step. It is the third workflow step, not an automatic release or separate fourth state.

## 6. GitHub quota and access failure

Claim rate exhaustion only with explicit 403/429 limit evidence, exhausted headers or Retry-After. Stop retries and report last verified SHA and reset time only if supplied. An ordinary 404/409, authentication error, transport failure or 5xx is not proof of quota. Continue genuinely independent work only from a reliable snapshot, verify HEAD after recovery and never invent successful commits or timing. A truthful blocker label may replace normal status temporarily.

## 7. Reporting and prohibitions

Briefly state substantive code, checks/blockers and next task; final line exactly one standalone bold 2–5-word truthful status. No USB/device-specific framework implementation, peripheral ELF direct firmware-I²C imports, USB-only catalog, package changes without version bump, `foo-v2` ID used as version substitution, split-file normal packages, signing revival, premature provisioning, fictional PASS, repeated owner tests or unauthorized merge/release. **Only the I²C bus ELF may temporarily delegate raw bus I/O to firmware; remove that last dependency in U3.**

## 8. Subsequent milestones

[U2](NEXT_MILESTONE_INDEPENDENT_PACKAGE_ECOSYSTEM.md) independently publishes packages, adds external sources and profile-aware system roles after accepted U1 and explicit owner direction; it is NOT a U1 Work Complete gate and must not begin during Improving Code. U2 preserves the I²C bus package and public capability while only its own private firmware import remains. [U3](NEXT_MILESTONE_T5_PRO_HARDWARE_DRIVER_MIGRATION.md) moves verified peripheral behavior into ELFs, replaces the I²C ELF's internal controller implementation and removes temporary firmware ABI; [U4](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md) handles provisioning and one actual CAM image. Reuse U1 streams rather than device-specific firmware transports. Start each subsequent milestone only on owner direction and an accepted baseline.
