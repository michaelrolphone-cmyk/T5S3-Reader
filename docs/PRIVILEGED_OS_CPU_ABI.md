# RiscRTE privileged OS/CPU ABI v1

**Authority:** [RiscRTE Platform](RISCRTE_PLATFORM_SPEC.md), [hardware-agnostic boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [driver loader scope correction](DRIVER_LOADER_SCOPE_CORRECTION.md), [provider graph admission/lifetime](PROVIDER_GRAPH_ADMISSION_LIFETIME.md). This describes the intended boundary and the experimental implementation in draft PR #78. **Ordinary package content validation and independent privileged admission are the active loader contract.** The package-signing experiment has been removed; it is not an optional prerequisite.

## 1. Hardware ownership and ABI

The CPU/OS port supplies generic interrupts, tasks, queues, synchronization, bounded allocation, logging, libc and low-level CPU pin-matrix primitives to independently authorized provider ELFs. It MUST NOT supply USB/I²C driver implementations, enumeration, transfers, charger commands, hardware-specific managers or fallback. The core treats capability IDs as opaque and never branches on `usb`, `i2c` or board IDs. Each provider ELF owns its physical hardware, callbacks and teardown and may consume other provider capabilities.

The privileged ABI is separate from the application ABI. Its exact 46-symbol inventory is `lib/elf_loader/include/private/privileged_os_cpu_symbols_v1.def`. Strong references make a missing symbol a firmware link failure. `esp_rom_gpio_*` are CPU pin/pad routing primitives, not firmware GPIO-device implementations. Privileged symbols are absent from ordinary app exports, customer tables and registered ELF symbols. A future structured port API is outside the current MVP.

## 2. Import inventory, declarations and resolution

`esp_elf_relocate_privileged_verified_v1` validates structural ELF metadata and calls `esp_elf_privileged_imports_valid_v1` BEFORE privileged scope or executable mapping. It inspects undefined global/weak imports from both `.dynsym` and `.symtab`, including tables with no relocations. It bounds symbol/string tables, checks missing/duplicate dynamic tables, malformed names/offsets, unsupported REL format, invalid RELA links and out-of-range symbol indexes. Imports must belong to the fixed libc compatibility set or exact 46-symbol generic OS/CPU inventory. Peripheral, firmware `t5_*`, customer and cross-ELF external imports are rejected.

`esp_elf_privileged_manifest_imports_match_v1` requires a strictly sorted, unique declaration of at most 128 import names, each 1–127 bytes, and exact equality to the actual undefined imports in BOTH tables. Missing, extra, duplicate, unsorted, malformed, forbidden or otherwise ABI-permitted but undeclared symbols fail before mapping. `SpecV2 -> ModuleV2 -> esp_elf_relocate_privileged_verified_v1` carries the declaration against a digest-checked ELF snapshot. **The declaration and SHA-256 are bounded consistency data, never independent authorization.** The privileged loader must also enforce trusted manager-controlled module admission, allowed OS/CPU ABI policy and execution-context/resource grants; a package does not need a cryptographic publisher signature to pass those independent rules.

**Global resolver bypass protection:** `elf_find_sym()` calls private-scoped `elf_find_sym_default()` while the current task owns scope, not mutable process-global `current_resolver`. Only the fixed OS/CPU inventory and libc resolve. No IDF/customer/registered/cross-ELF fallback occurs. Other tasks retain ordinary custom lookup. Production resolver tests include a malicious custom provider for `usb_host_install` and concurrent hook changes.

**Same-task nested relocation protection:** after verified preflight and `esp_elf_init`, the private loader entry calls `esp_elf_privileged_os_cpu_authorize_relocation_v1(module)` for the exact `esp_elf_t *`. Every public `esp_elf_relocate()` must call `esp_elf_privileged_os_cpu_relocation_enter_v1(elf)` before mapping. During an active privileged scope, a nested ordinary/unbound load or reentry of the same module fails with `-EPERM`. The exact module consumes its grant once. `relocation_leave_v1` runs on success and errors and checks mismatched leave. `end_v1` cannot release an active relocation; wrong-task scope operations fail. Normal loads in other tasks or outside privileged scope preserve normal behavior. Regression tests compile actual production C scope and relocation code. A global app-loading lock or custom-resolver swapping is not required.

These guards cover standard ELF loader paths, not arbitrary malicious in-process memory writes. Native ELFs share firmware address space; this is NOT an isolation sandbox. Only a firmware-owned manager/executor may activate privileged providers, and it must reject arbitrary user-supplied privileged specifications.

## 3. Package-manager handoff and exact candidate bytes — unsigned MVP

The required package manager accepts ordinary unsigned app/driver/service/provider packages from SD and online sources using the same bounded inspection, identity/version/ABI/dependency rules, SHA-256 integrity and recoverable staging/installation. A digest detects content mismatch but is not publisher authentication. The historical package-authentication experiment is no longer an active source or policy dependency.

The firmware-private executor must obtain **manager-validated, privately owned** package/provider identity, architecture, ABI, exact import names, dependency requirements, executable length and digest/byte reference. It independently checks allowed privileged imports, execution-context identity and active capability/resource grants. `SpecV2` stores the expected digest by value. `ModuleV2::loadVerifiedBytes` copies candidate bytes privately, validates the exact import/ELF image and relocates ONLY that same image. Full content hashing remains on install/update/recovery; installed hot-path inspection follows the explicit [verification-performance policy](U1_ELF_LOAD_VERIFICATION_PERFORMANCE.md). Use manager-controlled source lifetime or privately retained bytes to prevent a mutable SD pathname from substituting code after inspection. The digest and copied metadata alone do NOT confer privilege. Do not accept caller-supplied `SpecV2`, a forged manager receipt or unsigned manifest assertion as privileged authority, nor require a signed P-256 receipt to create legitimate manager-owned metadata.

Installed, inspected, admitted, mapped, activated and physically owned are separate lifecycle states. Package installation does not execute code or grant hardware permissions. A previously signed-only private function name is an implementation artifact, not a mandate to authenticate publishers; rename or adapt it during decoupling without bypassing the runtime policy.

## 4. Quiescence and exclusive physical ownership

Privileged providers must implement `quiesce()`. Failed start/teardown may leave IRQs, DMA, callbacks or hardware active; keep ELF mapped, deny new grants and pin dependency providers until quiescence is proved. Successful quiescence must drain callbacks, stop DMA/tasks/timers, remove IRQs, disable the peripheral and prevent outstanding pointers into unmapped code. Graph claims cannot arbitrate against compiled legacy USB, charger or `Wire`. Exclusive USB controller/PHY/VBUS/I²C0 cutover and physical electrical acceptance remain required.

## 5. Verification and remaining acceptance (September 17, 2026)

Earlier [experimental CI run 35269424152](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/35269424152) passed private resolver tests, USB/I²C PIC ELF builds and audits. [Firmware run 35269424045](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/35269424045) passed host tests and LilyGO build while T5S3-Pro post-job cleanup was pending at that check. Later graph, ELF audit and ABI tests must be judged at their own exact current commit; tests/builds do not establish physical acceptance.

Still required: working ordinary manager-to-private-executor handoff without mandatory signatures; valid, bounded ABI/import metadata and exact ELF-byte lifetime; permission/import/ownership mismatch tests; real physical USB/I²C ELF relocation, start/stop and legacy-owner exclusive cutover; VBUS/backfeed/current/role/rail, hubs/replug/duplex and ISR/DMA fault teardown. The removed provider-profile signing experiment supplies no runtime authority or acceptance requirement. Physical ELFs remain diagnostic, unpublished and noninstallable until functional, safety and on-board acceptance is established. Shipping USB/Serial Monitor, `Wire` and charger paths are unchanged.

## ABI 2: independently selected display-worker primitives

ABI 1 retains its frozen 46-symbol generic inventory. Scoped temporary exceptions are separately controlled; the old display proxy exception is retired by the M3 cutover. ABI 2 selects `privileged_os_cpu_symbols_v2.def`: the same 46 generic symbols plus `esp_intr_alloc_intrstatus`, `xQueueReceiveFromISR`, `uxQueueSpacesAvailable`, `vPortYield`, `esp_timer_get_time`, `esp_rom_delay_us`, three `risc_cpu_worker_*_v2` functions and `risc_cpu_cache_writeback_v2` (56 total). It does **not** inherit temporary I2C, SPI or video firmware-backend exceptions. Neither revision is an ordinary application export. Direct SDK function addresses do not enforce provider ownership after relocation; their use remains an audited obligation of independently admitted trusted native code.

A source manifest selects integer `os_cpu_abi: 2`, with exact `os-cpu-abi=2` in its digest-bound `provider-abi.v1` file. Historical missing manifest fields select only revision 1. Booleans, strings, null, unsupported revisions and manifest/profile mismatches are rejected. An absent legacy source manifest cannot select revision 2. Discovery accepts known revisions but grants no execution authority; the installed/bootstrap admission paths match metadata and the manager copies the selected revision into the owned graph specification. `ModuleV2` selects the corresponding private verified relocation entry. The common private scope records that selection; an ABI 1 scope never resolves ABI 2 names. Both revisions retain the same bounded exact import set across `.dynsym` and `.symtab`, immutable verified snapshot, independent manager admission, task-owned scope and one-shot module authorization. No customer or other-ELF fallback is added.

The worker API is a resident trampoline, not raw task-create/self-delete exports. Four generation-tagged descriptors may be live. Only the creating task can join/release; ISR and self-join are rejected. Start fully initializes a descriptor before the task becomes runnable; failed creation leaves a zero output and no future entry invocation. Early completion before start returns is supported. An entry must return normally and must not install provider-code TLS/deletion callbacks, throw through the trampoline, longjmp out or self-delete. Its final release-store is the worker's last descriptor access. Once join observes completion, provider entry and argument are never accessed again; self-deletion and deferred RTOS cleanup execute resident code. Zero timeout polls, 1–2000 ms waits cooperatively, larger values are rejected, and timeout retains ownership. Successful release consumes only the descriptor generation. It neither frees a TCB nor proves DMA/IRQ shutdown. IDF idle tasks separately reclaim task memory, so four descriptors is not a four-TCB aggregate-memory bound.

The cache primitive checks task/ISR context, nonempty length up to 1 MiB, overflow-safe exclusive PSRAM bounds, 4 KiB chunks and a 100 ms total deadline. A failed lock attempt yields rather than spinning with interrupts masked; byte and elapsed-time checkpoints yield during longer writes. It preserves the SDK CACHE-126 workaround and propagates ROM errors. The caller must own a live allocated buffer and keep it stable across yields and through subsequent DMA completion. Address checks prove the PSRAM interval, **not heap allocation ownership or lifetime**; native providers share the address space. This data-only writeback does not invalidate executable aliases or replace the loader's existing code-publication writeback/I-cache invalidation.

A display provider must disable its interrupt source, synchronize in-flight handlers on the allocated IRQ core, check IRQ/DMA deletion results and retain failed handles before freeing queues, buffers or mapped code. ISR callbacks and every reachable instruction/data object need a post-relocation placement audit; section annotations alone prove nothing. `vPortYield` is task-only; interrupt wakeups use `vPortEvaluateYieldFromISR`. Worker completion does not substitute for this IRQ/DMA proof. The existing display engines have not yet been cut over to ABI 2 in this checkpoint.

Software checks compile the actual worker and cache implementations. The worker race test unloads a native fixture and unmaps its argument after completion publication, reuses that descriptor for blocked work, then resumes the old resident cleanup. It also covers early completion, stale handles, wrong-task/ISR access, capacity, failed start and bounded joins. Import, scope, relocation, graph, manager and profile tests cover revision selection and rejection without widening ABI 1. These checks do not establish physical display acceptance.

## ABI 3: shared DMA channel reservation

ABI 3 adds `risc_cpu_dma_reserve_tx_v3` and `risc_cpu_dma_release_v3` to the
frozen ABI 2 inventory (58 symbols total). Exact integer manifest/profile
selection, preflight, import equality, manager admission and relocation scope
remain mandatory. ABI 1/2 never resolve these names; ABI 3 does not inherit
transitional bus or video exceptions. Ordinary applications cannot import them.

The ESP32-S3 CPU port uses the real resident SDK `gdma_new_channel`, channel-ID,
trigger connection and deletion APIs. SPI pairs and crypto channels therefore
share one occupancy table, trigger mask and controller clock reference count.
The reservation admits one TX direction, never its RX sibling. It stores no
provider callback, installs no IRQ, and performs no descriptors or transfers.
Only the returned group-0 channel may be programmed. Providers must not touch
global DMA reset/clock state or other channels. The audited S3 TX selector range
is 0–9 excluding RX-only selector 8; memory-to-memory is not admitted.

Five creator-task-owned, generation-tagged slots are bounded by the hardware
inventory. Failed allocation may return a nonzero retained token; the caller
must release it before unloading. A release requires task context, matching
creator and live generation, checks hardware TX idle, then disconnects/deletes
through the same SDK. It never stops an active transfer for the caller. Failure
retains ownership and clock lifetime; retries skip completed cleanup steps.
Provider IRQ removal and descriptor lifetime remain the provider's obligation.
These are trusted native-code obligations, not memory isolation.

Host fixtures compile the real CPU wrapper and provider TX helper, covering
shared SPI/crypto occupancy, exhaustion, cross-task/ISR/stale rejection, invalid
SDK IDs, busy/partial release, bounded stop, and retained failed-start tokens.
They do not replace physical DMA, cache or concurrency validation.

### Retired display proxy compatibility

The historical ABI-1 `display-epd-video@0.1.4` proxy imported the private
`t5_video_get_api` exception. That exception is now rejected at provider
admission and privileged relocation; it was never part of the frozen 46-symbol
inventory. Install the ordinary `display-epd-video@0.1.5` ABI-3 package together
with this firmware's independently built module store. The old proxy package
cannot run against this firmware. This is an intentional compatibility break,
not a claim that every old ABI-1 package retains its behavior. Legacy *application*
`t5_video_get_api` remains a consumer of the external capability, guarded by the
existing display takeover policy. No firmware display engine fallback remains.
