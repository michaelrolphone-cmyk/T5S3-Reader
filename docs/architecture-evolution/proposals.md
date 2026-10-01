# Architecture recommendations grounded in U1 to U4

R1–R4 identify **architectural questions**, not weights for the owner's parallel goals or permission to reorder or implement milestones. Roadmap dependency order and owner-controlled advancement remain, but U1 is not assigned greater goal priority. Close its remaining scope through reuse and focused evidence rather than repeated analysis, reimplementation or expansion. The [foundation analysis](foundation.md) explains the model and alternatives; [CrossPoint comparison](crosspoint-parity.md) separates product parity from app portability. Daily tool/clock/reader/game usefulness is a constraint on every change.

## R1 Prove a recoverable boot dependency closure and exclusive handoff

**Why this matters.** Downloadable drivers do not create a self-starting platform if the first module store, network path or error UI needs those same drivers. The roadmap already identifies this circularity. Current `main.cpp`, `HalStorage` and board initialization remain one coupled SD/display/board boot path; removing peripheral code without replacing its boot dependencies would strand the user's daily device [E10, E11].

**Recommendation.** Make the next architectural evidence a concrete dependency/ownership map for the selected Pro configuration and U3's actual driverless CAM route. For every boot-critical operation identify its source of bytes, dependency and authorization, physical owner, bounded failure behavior, recovery route and transition to normal provider ownership. Keep normal device behavior out of core; retain only a justified minimal bootstrap exception. Include cold boot and the desk clock's existing low-work timer-resume path: a smaller linked core that forces full SD/app/UI startup each minute is not automatically an improvement [E14]. Do not generalize one board's SD/power/pin layout into “any ESP32.”

**Alternatives.** The foundation analysis compares a narrow boot-media reader, a pre-staged independently accessible driver set, and permanently compiled normal-driver fallback. Prefer the narrowest verified route per actual port; reject fallback as the normal runtime solution. Prefer the existing SD bootstrap candidate for the verified CAM storage configuration, conditional on ordinary package integration and exclusive handoff. Keep no-SD and zero-PSRAM targets separately unproven; do not mandate one internal-flash store or add board support to U1 merely because diagnostic boards exist [E16]. The storage and memory contracts in [foundation](foundation.md#storage-and-memory-contracts-distinguish-recoverability-from-capability) are independent.

**Contracts and lifecycle.** Generic core owns package/context/grant generation and opaque exclusive claims; bus/chip/rail ELFs own hardware. Boot ownership must cease before normal ownership begins, with no live files, DMA or callbacks left behind. Missing optional UI/media leads to recoverable idle. Missing required hardware denies dependent activation. Failed quiescence retains pins and mapping; it never licenses a duplicate owner.

**Resource and compatibility limits.** Inventory the linked boot image, package-store capacity, peak loader working set and minimum diagnostic/recovery reserve. Preserve U2's package identity/public bus APIs through U3 native cutover. Source-compatible capabilities do not imply one ELF or firmware binary across ISA/cache/flash-layout differences [E13]. Preserve U1's required generation-bound integrity receipt/invalidation work; do not confuse that local cache with the prohibited package-authentication/signing system or add a second installer.

**Staged evidence and falsification.**
1. During U1, verify the actual I²C→BQ VBUS→USB graph and stream/package path with the existing single-owner rules; do not add unrelated display/CAM work.
2. At U3, an authorized driverless boot witness must show idle/recovery without display, touch, optional SD or GUI allocations/ticks. Inspect the linked/call graph as well as a build; no physical pass is implied here.
3. At U4, provision from the *verified* available route and save one real image, preserving Pro.
Falsify the chosen bootstrap route if it needs its own unavailable driver, leaves simultaneous controllers active, cannot recover an interrupted required-package replacement, or loses an existing daily-use recovery path.

**Success measures.** Every retained boot hardware call has a named reason and exclusive release boundary; no graph cycles; no normal provider remains available after its ELF is absent; an interrupted change preserves a reachable last-good state. Count these outcomes alongside core bytes and idle memory, not package count alone.

## R2 Define a real product application boundary with a CrossPoint parity contract

**Why this matters.** Current “apps” can be firmware entry points in disguise: settings delegates rendering/activation, file transfer enters a compiled activity, and the reader remains compiled. A growing feature-specific host ABI can preserve UX while preventing core reduction or upstream reader deployment. Moving files to repositories does not solve that boundary [E12].

**Recommendation.** Identify the intended upstream CrossPoint revision and deployment/build contract, then group reader/product policy into a coherent app or product bundle over a small reusable capability interface. Keep games, tools and other products free to consume the same lower-level facilities. Use the [feature map](crosspoint-parity.md) to preserve behavior, including persistent data, rather than treating a launched screen as parity.

**Alternatives.**
- Keep compiled-but-optional GUI/product code: correct U3 intermediate and low-disruption baseline, but not the final minimum-core product split.
- Coherent reader/app bundle plus a narrow source-facing service adapter: preferred direction for further evidence. Hardware/storage/network/input/power remain capabilities; product menus, reading policy and content state do not become new core APIs per feature.
- One package per tiny reader function or an immediate rewrite: defer. There is no measured benefit sufficient to justify extra lifetime, memory and update dependencies.
- Load an upstream complete firmware image as an app: not supported by the inspected `app_main` ABI and CPU-target admission. A source adaptation/build is a separate deliverable, not an automatic binary compatibility claim.

**Contracts and ownership.** The product owns reading/layout/settings policy and versioned private content state. Common reusable services own only genuinely shared mechanisms. Runtime controls scoped storage/device rights and trusted mediation; a product cannot bypass them for convenience. File, font, content and navigation lifetimes must outlive asynchronous work appropriately and stop before unload. Preserve current bookmarks/progress/configuration formats or specify a reversible migration; no deletion to achieve “small core.”

**Compatibility and resources.** U2 already requires pinned SDK/toolchain/build inputs and compatible independent releases: treat missing evidence as a U2 gap, not a new proposal. Use a bounded C/handle/service interface where module lifetime or toolchain ABI makes shared C++ object ownership unsafe. Measure app/firmware flash and peak RAM together so moving code does not merely duplicate it. Target-specific builds may share source/contracts; current S3 ELF checks do not prove C3 portability [E13].

**Staged evidence and falsification.** Preserve U3's optional compiled GUI; do not require full extraction to finish U3/U4. Once the owner scopes a product-boundary experiment, build one pinned reader path against a thin adapter and record what still imports product-specific firmware. Exercise a book open/render/page-turn/save/exit/resume path, then representative transfer/settings behavior and the selected upstream parity gaps. Falsify the adapter if it requires adding reader-specific core calls for every upstream feature, cannot preserve saved state/UX, or makes daily launches materially worse. A clean demo that omits expected features is insufficient.

**Success measures.** A reviewable feature-to-capability matrix; a reproducible intended upstream build/port; user-visible parity evidence at that baseline; reduction of product-specific linked firmware symbols without losing tool/game/reader/clock use. Neither full parity nor an agreed upstream app target is established today.

## R3 Preserve a high-throughput data path while centralizing ownership and admission

**Why this matters.** Fast e-paper is an independent product goal, not just a compatibility test. A generic architecture that inserts copies, lock contention, package lookup or extra wakes into rendering can erase the useful behavior it was meant to generalize. Current rendering already overlaps work with scan and has stage timing; the roadmap explicitly allows owned frame buffers [E10, E14].

**Recommendation.** Keep generic policy/resolution in a control path at acquisition/session boundaries; let a pinned, bounded provider-owned data path exchange owned frames/chunks directly. Move actual waveform/DMA/power lifecycle into the provider without requiring per-pixel dispatch or a universal compositor. Reuse this separation for tool serial/programmer streams and future LCD/camera consumers where appropriate, while keeping each data semantic explicit.

**Alternatives.** Retain the firmware video bridge temporarily as an evidenced baseline; migrate behind a stable surface while retaining efficient buffer handoff; or force all workloads through one copied byte/record pipeline. Prefer the second only when authorized and measured; reject the third as a blanket rule. This is applying the existing U1–U4 stream allocation, not creating Stream v3.

**Contracts and failures.** Separate buffer reuse, submission completion, optical/device settling and full quiescence. Providers advertise supported formats, queue/presentation behaviors and budgets; clients adapt or fail clearly, without board-name routing. Retain optional low-latency e-paper behavior rather than forcing LCD/AMOLED to emulate pulses. DMA, callbacks and shared power pins remain owned until verified stop; one departing consumer cannot switch off another consumer's rail. Current PR #220 COMPLETE is accepted flip, not optical settling; preserve that useful low-latency distinction instead of making every submit wait for the panel. The historical void-stop/quiescence question below remains a separate lifetime issue, not proof of a current use-after-free [E18].

**Resource budget.** Attribute candidate snapshots, relocation copies, mapped images, frame/state/DMA buffers and quarantined allocations to their actual lifetimes. No universal PSRAM or framebuffer budget. Existing app ledger and graph pinning remain; add reservation machinery only after a meaningful pressure/admission problem is measured. Account for shared-controller/pad constraints as well as byte counts.

**Staged evidence and falsification.** Compare an identical expressive scene/quality before and after one ownership boundary moves: render/pack/copy/wait, scan preparation/DMA/pacing, input latency, peak memory, idle behavior and app return. Retain the existing no-copy fast path when the buffer is free. Test concurrent daily tool/input activity, failed stop, format change and provider removal. A higher scan counter is not proof of completed presentation; host raster timings are not device FPS. Falsify a proposed abstraction if it adds avoidable full-frame copies, per-frame discovery, unsafe lifetime, lost visual behavior, or unacceptable measured interaction/power regressions.

**Success measures.** No new mandatory full-frame copy without demonstrated benefit; bounded data-path latency and cancellation; truthful completion; retained game/clock/tool/reader UX; profile-specific display and input semantics demonstrated when actual LCD/watch hardware is selected.

## R4 Make developer usability a reproducible package round trip

**Why promote this now.** The 1.0 beta planning target makes an independently reproducible use path more decision-useful than another broad abstraction. External repositories already build with pinned inputs; Drivers now checks actual bytes and executable modes. That supersedes the older “no independent builds” snapshot, but neither result establishes canonical ZIP delivery, installation, ABI use or recovery [E18]. U2 already requires independent sources/builds; this is a concrete missing witness, not an invented ecosystem milestone.

**Recommendation.** Use one existing useful app and its real required provider closure on an explicitly supported runtime/storage/memory target. Record immutable runtime, SDK/toolchain, package-source and dependency revisions; exact imports/ABI; archive ID/version/hash and source identity; available memory classes and store/recovery space. From a clean isolated checkout, build without hidden Reader workspace state, produce the ordinary canonical archive, inspect/install it through the existing manager, run its actual capability-dependent flow, update to a strictly newer version, then demonstrate refusal or recovery of a corrupt/interrupted candidate with the prior usable generation and user state preserved. Reboot/relaunch evidence must identify the selected generation. A local test source is sufficient for initial software evidence; actual publication, device operations and physical qualification remain separately authorized.

**Simpler retained design.** Keep Reader's currently working package source and compiled-optional GUI while proving independent builds and ordinary intake. Test the candidate archive directly before introducing repository switching. This separates packaging defects from source-registry policy and avoids freezing package development until every reader feature is extracted. Retaining the current source is an explicit migration baseline, not final U2 acceptance. Reuse existing transaction, package-use gates, inventory and recovery; do not add another manager, developer template, marketplace, signing system or broad test gate.

**Compatibility and resource risk.** Test a real required import/capability, not only an empty hello-world. Compare old/new package requirements with the pinned runtime; identical source bytes cannot prove compatibility with a different SDK or loader. Preserve canonical identities and data; do not republish changed bytes at the same version or silently rebind installed origins. Account for old/new/staged storage generations and peak loader/provider/frame memory together. A no-PSRAM port remains unsupported until executable allocation and useful workload evidence exist.

**Falsification and useful outcome.** The witness fails if it needs the maintainer's mutable checkout, a firmware rebuild for an otherwise compatible package update, manual file replacement outside the manager, a hidden compiled hardware fallback, or destroys the last usable generation/state. Record the last proven step rather than calling a compile “developer-ready.” One witness does not certify all four kinds, all boards, CrossPoint parity or U1 completion; it makes the remaining beta claims specific. Keep book open/page/save/resume, expressive rendering/exit, clock wake and serial/programmer recovery as product constraints, using existing evidence while daily T5S3 testing is deferred.

## Supporting contract findings from the initial review

The findings below remain useful, but are **not separate top-level priorities**. The initial review over-weighted local contract issues and insufficiently accounted for U1–U4. Their source revisions and limitations remain in the evidence register.

### S1 Display completion and quiescence

Historical snapshot: at PR #220 `e0031d06`, the adapter interprets a scan-counter advance as completion even though the backend advances it at scan start, and returns quiesce success after a void stop that can leave hardware resources retained. Current wrappers discard the token and the host has a separate force-stop restoration guard, so this is not a demonstrated current use-after-free [E5].

The current `0987497c` checkpoint is green and documents accepted flip rather than physical settling; do not re-report the historical wording as a newly found optical-completion defect [E18]. Retain separate writable-frame ownership, submission completion and physical-stop guarantees; failed stop must retain code/dependencies/buffers. The existing SDK has geometry/formats/feature metadata, but its helper is MONO1/GRAY2 and its EPD provider is 960×540 and ignores presentation options [E9]. Use that evidence inside R3; do not replace working rendering with a universal compositor.

The source-derived EPD pixel/state peaks are 907,200 bytes MONO1 and 1,296,000 GRAY2 including startup scrub, plus explicit 720-byte DMA rows and 8,192-byte scan task stack, excluding other allocations. They are not measured whole-system peaks or universal display budgets [E5].

### S2 ABI compatibility and release provenance

Minimum app API declarations are intentionally translated to exact selected graph versions; that is not a resolver mismatch bug. Existing root ABI size/prefix rules and generation/pin lifetimes are useful. The historical U1 CDC seam at `74c7bdfd` remains important: both 0.1.0 and 0.1.7 advertise serial.port API 1, while that snapshot's consumer requires the appended endpoint suffix and refuses it when absent [E1, E2, E6]. This is not a claim that the active `8d8f2472` runtime still has exactly the same compatibility behavior.

Preserve binary/behavior contracts, distinguish mandatory behavior from optional appended features, and use small mixed-version witnesses with immutable SDK/runtime/package inputs. All 22 overlapping Drivers SDK headers matched the inspected Reader baseline: no current SDK drift was found. Independent-build provenance is already U2 work, not justification for a new SemVer/SAT resolver or separate milestone.

### S3 Aggregate memory and retained resources

The 4096-entry app allocation ledger reclaims tracked app allocations and preserves failed-realloc ownership; provider/firmware allocations are outside it. Graph-owned candidate bytes can overlap a temporary relocation copy and mapped image, with fallback between memory classes. A per-image 8 MiB validation limit is not an aggregate admission budget [E3, E4].

Measure those lifetimes with the data path and preserve control/recovery reserves. Charge quarantined resources until they can safely stop. Prefer existing ownership/failure handling when measured margins suffice; do not add reservation machinery or storage VM merely to make the design look complete. Storage VM does not transparently page ELF stacks/data or DMA buffers.
