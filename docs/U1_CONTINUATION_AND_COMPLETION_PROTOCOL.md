# Recommended U1 continue-driven implementation protocol

**Advisory workflow, September 18, 2026.** Read [root AGENTS.md](../AGENTS.md), [implementation-first workflow](IMPLEMENTATION_FIRST_QUALIFICATION_WORKFLOW.md), [U1 milestone](NEXT_HARDWARE_TEST_MILESTONE.md), [four-milestone order](FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md), [I²C bus ELF cutover](I2C_BOOTSTRAP_CUTOVER.md), [SPI/UART bus ELF cutover](SPI_UART_ELF_BOOTSTRAP_CUTOVER.md), [streams audit](STREAM_PIPE_MILESTONE_ALLOCATION.md), and [status reporting](U1_STATUS_REPORTING.md) as the normal context.

This document describes the preferred U1 sequence and reporting model. Use it as the default when the current task does not specify a different approach.

The default implementation style favors completing meaningful code blocks without repeated owner-test or CI-wait loops while preserving identity/version integrity, honest build status, recovery behavior, and the intended architecture.

## 1. Preferred implementation context

[PR #86](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/86) is historically a specification baseline rather than implementation. Under the normal flow, verify its merge state and use current master plus one U1 implementation branch/PR. Prefer real source/commit inspection over treating specifications as implemented.

A concise `docs/U1_IMPLEMENTATION_LEDGER.md` on the implementation PR is useful for completed/remaining work, commits, versions, checks/blockers, and next source action. The ledger is supporting coordination, not a substitute for coding.

If the current task calls for a different base, branch, PR structure, temporary experiment, or accelerated integration path, use that task-specific path.

## 2. Normal meaning of `continue` before Work Complete

Under the standard workflow, an owner `continue` means substantive next implementation work on the same milestone PR: inspect, implement, commit, run proportionate checks, and fix known blocking defects. Prefer continuing independent work while CI runs or unrelated checks fail rather than waiting for repeated checkpoints or intermediate hardware tests.

### Preferred internal sequence

The intended sequence is:
0. minimal linked-source inventory;
1. generic ELF-published byte/record Stream/Pipe endpoints, cross-context rights, and safe lifecycle;
2. installed I²C bus ELF with stable public interface and private transitional raw-transfer backend, followed by functional USB controller/host/class/power ELFs and Serial Monitor/programmer on `serial.port`;
3. generic-stream online/SD ZIP intake plus four-kind manager/version/catalog/source work;
4. integrated software handoff.

Signing-purge work and independent repairs may run in parallel. The generic ABI should support later SPI/UART bus providers and bus-only private imports. SPI/UART drivers are normally added to U1 when a real U1 dependency benefits from them, rather than solely for completeness. If a U1 driver needs one of those buses, the preferred order is to publish its bus ELF first with that ELF owning the transitional firmware port. See [SPI/UART cutover](SPI_UART_ELF_BOOTSTRAP_CUTOVER.md).

The intended U1 feature set includes provider-owned USB, generic `serial.port`, a non-USB witness, four-kind online/offline lifecycle, `.rte.zip` bundles and per-ID layout, ZIP bootstrap/service, safe legacy and CDC ID migration, generic catalog/release plumbing, versions, signing purge, and installed-ELF performance. First-run provisioning is normally later work.

### Preferred bus ELF boundary

[I²C Bus ELF Cutover](I2C_BOOTSTRAP_CUTOVER.md) describes the intended U1 model: an installed I²C bus ELF publishes `i2c.bus` or a compatible versioned contract and privately owns the transitional firmware I²C controller/transfer primitives. Power/expander/USB-supporting and other I²C device ELFs normally consume that public capability and own their device-level policy.

The preferred architecture avoids direct firmware I²C/`Wire`/`kernel.i2c` imports from peripheral ELFs. The bus ELF mediates rights, transactions, and controller ownership; missing capability normally fails closed. Record package ID, public ABI, private importer, actual dependency graph, startup/timeouts, and intended backend replacement in the implementation ledger. Native I²C controller ownership is normally a later U3 concern rather than a U1 completion gate.

SPI/UART follow the same preferred provider pattern. Their device migration is normally U3 unless a concrete U1 dependency benefits from earlier use. An SPI or physical-UART bus ELF preferably publishes the stable public capability before dependent device ELFs migrate; the bus ELF privately owns any transitional raw firmware backend. U3 normally swaps that backend for native controller ownership without changing upstream consumers. USB CDC `serial.port` and ESP32 hardware UART remain distinct concepts. Isolated boot/ROM diagnostics may remain where useful.

A current task can intentionally prototype or test outside this normal sequence.

### Preferred stream integration and performance

Preserve useful merged byte v1/record v2/App Store/GNSS behavior while completing provider-owned endpoints, cross-context access, bounded backpressure, revocation, and quiescence. Prefer actual USB ELF RX/TX for Serial Monitor/programmer and common bounded cancellable online/SD ZIP transfer paths. Avoid blocking provider I/O under the global stream mutex and avoid unnecessary downstream copies.

For [ELF Load Verification Performance](U1_ELF_LOAD_VERIFICATION_PERFORMANCE.md), the intended optimization is to avoid repeated whole-file SHA/MD5 for ordinary committed launch/inventory while retaining install/recovery/explicit-verification, capability/ABI checks, generation-bound validation, and coherent fast inventory. UI actions should preferably avoid synchronous whole-ELF rehashing.

Use targeted ZIP/path/bounds, integrity/rollback, import/capability, version/identity, lifetime, and bus-boundary checks where useful. Report failed or unrun checks accurately.

## 3. Work Complete

Under the normal workflow, use **Work Complete** when the intended production implementation is connected and no known blocking source/build defect remains. Report the PR/commit plus checks run and omitted. This label communicates implementation state; it does not itself determine physical acceptance, merge, release, or publication.

Avoid using the label for docs-only or knowingly incomplete implementation unless the current task intentionally defines a different milestone meaning.

## 4. Improving Code

Under the normal workflow, a later `continue` after **Work Complete** means useful review, improvement, targeted checks, performance work, diagnostics, or defect repair on the same PR. If a blocker shows that implementation is incomplete, return to implementation work and correct it.

## 5. Release Qualification

Release Qualification is normally a collaborative hardware/integration phase when it is part of the current task. Diagnose, patch, repeat meaningful checks, and identify the tested revision/assets from actual evidence.

Merge, tag, release, and flash are normally separate delivery actions from qualification. A current task can combine, reorder, or skip these phases.

## 6. GitHub quota and access failures

Treat explicit 403/429 rate-limit responses, exhausted headers, or Retry-After as strong evidence of quota exhaustion. Generic 404/409/auth/transport/5xx failures are better classified by their actual error.

After confirmed rate limiting, avoid wasteful repeated calls and report the last verified SHA/reset time only when actually available. Continue independent work from a reliable snapshot where useful and verify HEAD again after recovery. Transport cancellations and tool-level denials are execution-layer results; record them as such.

## 7. Reporting and discouraged deviations

The preferred status report briefly states changed code, checks/blockers, and next task, ending with one truthful short status label when that convention is useful.

The standard U1 architecture generally avoids firmware-owned USB/device implementations, direct firmware bus imports in peripheral ELFs, USB-only catalogs, unchanged-version republishing, `foo-v2` identity workarounds, split-file normal distribution, signing revival, and premature provisioning. Temporary raw I²C/SPI/UART firmware operations are preferably isolated behind their installed matching bus ELF, with U3 replacing those transitional backends.

These are repository design recommendations. A current task can intentionally request an exception or experiment.

## 8. Subsequent milestones

The normal roadmap treats [U2](NEXT_MILESTONE_INDEPENDENT_PACKAGE_ECOSYSTEM.md) as independent package publication/external sources/profile-aware roles after U1; [U3](NEXT_MILESTONE_T5_PRO_HARDWARE_DRIVER_MIGRATION.md) as fitted-peripheral migration plus native bus-controller ownership; and [U4](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md) as provisioning and real-image capture.

Reuse U1 generic streams where practical. This roadmap is the preferred sequence; a current task can reorder or prototype later work earlier.
