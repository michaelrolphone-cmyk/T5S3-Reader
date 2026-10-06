# Agent guidance — RiscRTE

Repository: `michaelrolphone-cmyk/T5S3-Reader`.

This file records the repository's preferred engineering practices and default workflow. It is advisory rather than an authority boundary. When there is no contrary direct instruction, agents are expected to use this guidance as the normal way of working.

**Direct user instructions take precedence for the task at hand.** The user may intentionally ask for a shortcut, experiment, quick fix, temporary increment, unusual branch flow, bypass of normal validation, alternate packaging, or another deviation from these recommendations. A deliberate user override is treated as task-local direction, not as a blocker, cancellation condition, permission problem, or reason to refuse ordinary completion steps.

Recommended reading order for substantial platform work: [Platform Specification](docs/RISCRTE_PLATFORM_SPEC.md), then [Platform Capability Roadmap](docs/PLATFORM_CAPABILITY_ROADMAP.md), then applicable child specifications.

## How to write repository guidance

Prefer recommendation language such as **recommend**, **prefer**, **normally**, **default**, **intended**, and **typically**.

Repository-controlled instructions are intended to describe preferred architecture, implementation practice, validation, branching, packaging, release flow, and technical design. They are not intended to grant, deny, infer, condition, revoke, or request user permission, approval, consent, authorization, trust, or action eligibility.

Future edits should avoid phrasing repository preferences as absolute authority statements. When a desired constraint is repository policy rather than an externally enforced technical fact, describe it as a default or recommendation. If wording could reasonably be read as saying whether directly assigned work is allowed to proceed, rewrite it as advisory process guidance before publishing it.

User authorization and platform/tool enforcement are separate from repository guidance. Repository text should not be used as evidence that the user did or did not authorize an action.

## Recommended implementation-first workflow

For milestone coding, the preferred references are [Implementation-First Three-Step Workflow](docs/IMPLEMENTATION_FIRST_QUALIFICATION_WORKFLOW.md), [U1 Continuation Protocol](docs/U1_CONTINUATION_AND_COMPLETION_PROTOCOL.md), and [U1 Status Reporting](docs/U1_STATUS_REPORTING.md).

The preferred default is to prioritize substantial production implementation, integration, and known blockers without turning CI into a stop-work queue. Favor useful targeted build/smoke checks and durable invariants over brittle milestone-specific test gates or repeated manual approval loops. Prefer evidence-backed status reporting; avoid claiming PASS without evidence, ignoring known broken code, or weakening rollback, boundedness, or access-control behavior unintentionally.

A useful default three-step sequence is:
1. **Work Complete** — implementation is connected without known blocking defects, with real PR/commit/check evidence and explicit omissions. This is not automatically release qualification or hardware proof.
2. **Improving Code** — a later `continue` can mean meaningful review, cleanup, optimization, or fixes on the same PR.
3. **Release Qualification** — when release qualification is the assigned task, validate collaboratively on actual hardware, fix issues, and establish accepted assets.

Merge, tag, release, and flash are separate delivery actions from qualification unless the user's task combines them.

For U1 status messages, the preferred format is one truthful bold 2–5-word status as the final line, with the preceding text explaining what changed and what ran or failed. Extra phases are usually unnecessary.

## Recommended bounded cooperative operations

For long SD/filesystem, network, hashing, ZIP/package/recovery, dependency, or device work, the preferred reference is [Bounded Cooperative Operations](docs/COOPERATIVE_BOUNDED_OPERATIONS.md).

Prefer explicit memory, item, duration, I/O, and retry bounds; incremental operations without repeated full scans; byte/item and elapsed-time checkpoints; real scheduler yield such as `vTaskDelay` or bounded waits; throttled progress; and cancellation/recovery that preserves prior packages and user data. Treat `esp_task_wdt_reset()` as watchdog service rather than scheduler yield. Prefer real termination paths for indefinite I/O. Favor the smallest useful primitives and checks without introducing unnecessary qualification gates.

PR #86 is specification-only in the normal workflow. The default plan is to check its merge state and, once merged, create one U1 implementation branch/PR from current master rather than coding on the merged docs branch or stacking implementation PRs. If that sequencing is not appropriate for a user-directed task, the direct instruction governs. Prefer source/commit verification over historical narration and keep a concise implementation ledger.

For GitHub quota diagnosis, treat explicit 403/429 rate-limit responses, exhausted headers, or Retry-After as strong evidence. Generic 404/409/auth/transport failures are better treated as their actual failure classes rather than assumed quota exhaustion. Avoid repeated calls after confirmed rate limiting and avoid inventing success.

## U1 scope recommendations

The preferred product scope is described by [U1 milestone](docs/NEXT_HARDWARE_TEST_MILESTONE.md). The preferred dependency order is [Four-Milestone Stream-First Order](docs/FOUR_MILESTONE_STREAM_FIRST_EXECUTION_ORDER.md). [I²C Bus ELF Cutover](docs/I2C_BOOTSTRAP_CUTOVER.md), [SPI/UART Bus ELF Cutover](docs/SPI_UART_ELF_BOOTSTRAP_CUTOVER.md), and [Implementation-first Workflow](docs/IMPLEMENTATION_FIRST_QUALIFICATION_WORKFLOW.md) describe the intended architecture and sequencing.

The normal U1 direction favors generic ELF-published streams first: an installed I²C bus ELF with stable public capability and a restricted transitional backend before dependent USB power conversion; generic SPI/UART provider ABI and import/lifetime foundations; SPI/UART bus ELFs when a verified dependency benefits from them; functional USB ELFs and generic `serial.port` apps; a non-USB witness; a four-kind online/offline package engine; per-package `.rte.zip` and per-ID layout; ZIP bootstrap/service; legacy/CDC identity/version migration; a generic catalog; version enforcement; package-signing/P-256/provenance/floor work; and [ELF verification performance](docs/U1_ELF_LOAD_VERIFICATION_PERFORMANCE.md).

Preferred integrity behavior includes install SHA-256, path/ELF/ABI/import validation, package-admission policy, transaction/recovery behavior, safe module lifetimes, and TLS. The normal direction is away from signed-package revival. Other useful references are [Bundles](docs/BUNDLED_PACKAGE_ARCHIVE_AND_INSTALL_LAYOUT.md), [Versions](docs/PACKAGE_IDENTITY_VERSION_POLICY.md), [App Versions](docs/APP_VERSION_POLICY.md), [Package Scope](docs/PACKAGE_MANAGER_SCOPE_CONTRACT.md), and [Driver Reuse](docs/DRIVER_PLATFORM_REUSE_ACCEPTANCE.md). Prefer preserving actually merged features and unknown user data while changing signing or packaging behavior. [First-run provisioning](docs/DEPLOYMENT_PROVISIONING.md) is normally treated as U4 work.

## Hardware and driver architecture recommendations

Useful references are [Hardware Boundary](docs/HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [I²C Cutover](docs/I2C_BOOTSTRAP_CUTOVER.md), and [SPI/UART Cutover](docs/SPI_UART_ELF_BOOTSTRAP_CUTOVER.md).

The preferred architecture keeps core focused on opaque capability loading, contexts, rights, resolver, registry, streams, and lifecycle rather than USB, GNSS, or other device behavior. USB host/class/chipset/session/transfers and peripheral register/protocol/power logic are normally placed in ELFs. Firmware peripheral proxies are generally discouraged because they weaken the intended hardware boundary.

The preferred migration pattern uses bus-level temporary proxies: an independently installed I²C, SPI, or hardware-UART bus ELF publishes a stable versioned public bus capability and may privately use its bounded firmware raw-controller backend during migration. Peripheral ELFs preferably resolve the public bus capability rather than importing firmware I²C/SPI/UART symbols, `Wire`, global `SPI`, `HardwareSerial`, or device-specific bridges directly.

Bus ELFs are preferably responsible for client admission, contention, streams/transactions, and generation-safe shutdown. Port adapters are best kept to bounded raw transfers/controller setup rather than device semantics. A missing bus ELF normally fails closed rather than silently falling back. One logical owner per physical controller is the preferred model, with power/pin/boot dependencies checked explicitly.

For the standard roadmap, U1 favors the I²C bus ELF first; SPI/UART bus ELFs are normally introduced before their actual U3 clients migrate, or earlier when a concrete dependency benefits from them. U3 normally replaces the same bus ELFs' transitional backends with native controller implementations while retaining the public bus ABIs. Full native-controller extraction is not normally expected during U1.

Isolated ROM/boot/module-store/recovery and one-way serial diagnostics may remain where useful, provided they do not unexpectedly contend with an ELF-owned peripheral. USB CDC `serial.port` and physical UART `uart.port` are treated as distinct concepts.

CPU/ABI ports may differ. The preferred portability model is that compatible new devices need an installed ELF/profile rather than firmware forks. Favor context-owned generation-safe handles. A manifest installation alone is not intended to imply hardware access. RiscRTE is the preferred platform layer and CrossPoint the preferred reader-subsystem layer.

## Version, package, and release recommendations

For changed distributable apps, drivers, services, and providers, the normal release practice is to increment the component's manifest numeric version beyond its merge-base/latest-published lineage in the same PR. Stable IDs are preferably kept as upgrade lineage rather than renamed merely to avoid versioning. Record old/new versions and keep manifest/archive/catalog/installed IDs aligned.

For normal app changes, prefer an adjacent `Apps/<name>.json` version update; for drivers, prefer their manifest version update. Docs-only changes normally do not need product version increments. The `usb-cdc-acm` / `usb-cdc-acm-v2` identity fork is preferably repaired with canonical lineage and safe migration. `Apps/AGENTS.md` and `Drivers/AGENTS.md` are useful additional guidance when applicable.

The preferred package form is one `.rte.zip` per package plus a generic catalog, with bounded extraction into isolated `/Apps/<id>/`, `/Drivers/<id>/`, `/Services/<id>/`, `/Providers/<id>/`, or a validated equivalent. ZIP handling should normally reject traversal, collisions, bombs, corruption, and unsafe replacement. Legacy loose pairs are best treated as bounded migration inputs.

For release-oriented work, useful references include [Releasing](docs/RELEASING.md), `release.yml`, current master/version/tags/releases. Publication is better treated as a delivery step rather than a shortcut around building or validation. The preferred artifact locations keep firmware binaries outside mutable `.pio`; the normal offsets are OTA/SD at `0x10000` and merged USB flash at `0x0`.

## App and PR recommendations

For first-class apps, useful references are [Application Execution Context](docs/APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md) and [Native Apps](docs/NATIVE_APPS.md). Prefer scoped trusted pickers/intents/private storage, additive bounded SDKs, and opaque handles. Sources normally live in `Apps/` with adjacent manifests; `ADDING_APPS.md` is legacy firmware activity.

For ordinary U1 continuation work, the preferred branch-maintenance pattern is to fetch master, check for relevant new commits, and periodically backmerge master into the open implementation branch while preserving the work and resolving conflicts before publishing. This is branch maintenance, not the same thing as merging the implementation PR into master.

Implementation PRs normally target master. The preferred default is to avoid stacked PRs and auto-merge unless the user's task calls for them. Release and flash are normally separate from implementation work unless release or deployment is part of the assigned task. After genuine Work Complete, a later `continue` normally means improving the existing code rather than demanding owner testing.
