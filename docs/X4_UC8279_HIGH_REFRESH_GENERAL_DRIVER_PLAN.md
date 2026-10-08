# X4 Pro UC8279 high-refresh general-use driver plan

**Status:** implementation and qualification specification  
**Stable mechanism baseline:** X4LAB v0.1.5, commit `05d811ae3a75b0711540484ccdbee32464042dd6`  
**Permanent baseline pointer:** `stable/x4-high-fps-v0.1.5`  
**Production integration target:** `Drivers/x4pro_panel`  
**Initial supported controller:** ZHX UC8279 `LUT_VER=0x68`  
**Quarantined destructive branch:** `quarantine/x4-high-fps-v0.1.6-destructive`

## 1. Objective

Convert the high-refresh mechanism proven by X4LAB v0.1.5 into a normal RiscRTE `display.output` driver that can accept arbitrary application framebuffers and damage rectangles, survive normal boot/sleep/reset/fault lifecycles, manage ghosting and controller state, and fall back safely when the fast path is not valid.

The production driver must preserve the ordinary X4 display contract. Applications should submit frames through the existing display API; they must not know UC8279 commands, LUT bytes, waveform timing, panel RAM layout, or cleanup cadence.

This is not a request to carry the v0.1.6 experiments forward. v0.1.6 crossed a destructive boundary and produced persistent gray blocks in two display columns. Its compact `TRES/GSST` geometry, reduced or zero TCON timing, 80 MHz transport, and undocumented PLL probe are excluded from this design.

## 2. Proven v0.1.5 operating envelope

The following envelope is the only fast baseline eligible for conversion:

- controller: double-probed ZHX UC8279, initially `LUT_VER=0x68`;
- controller geometry: normal 800x600 controller space with the visible 800x480 panel at the established 120-row offset;
- region selection: ordinary UC8279 partial-window commands only;
- SPI: 20 MHz hardware SPI;
- PLL: `0x0F` for fast operation and `0x0E` for the known clean baseline;
- waveform: external one-frame or two-frame A2-style LUT;
- image preparation: cached or already-rendered row data, not per-bit GPIO rendering during transfer;
- artifact management: temporal Bayer phases for content that is eligible for dithering;
- optional high-rate state model: absolute target drive with no DTM1 synchronization during an active burst;
- voltage behavior: no changes to VCOM, booster, source/gate voltage, PMIC, or battery configuration;
- abort behavior: bounded BUSY waits and a controller-reset escape path.

### 2.1 Hardware results that justify conversion

| Mode | Region | State model | Measured result | Production interpretation |
|---|---:|---|---:|---|
| Two-frame Bayer | 800x160 | Differential, DTM1 synchronized | 13.01 FPS | Conservative fast partial profile |
| One-frame Bayer | 800x160 | Differential, DTM1 synchronized | 15.35 FPS | Interactive differential profile |
| Two-frame Bayer | 800x160 | Absolute, no DTM1 sync | 14.22 FPS | Quality-oriented absolute profile; not the fastest option |
| One-frame Bayer | 800x160 | Absolute, no DTM1 sync | approximately 17.07 FPS | Primary bounded motion profile |
| One-frame Bayer | 800x80 | Absolute, no DTM1 sync | 20.95 FPS | Local animation/scrolling profile |
| One-frame Bayer | 800x40 | Absolute, no DTM1 sync | 23.65 FPS | Small-region upper bound, not the general performance claim |
| One-frame Bayer | 800x480 | Absolute, no DTM1 sync | 9.79 FPS | Full-visible-panel motion capability |

The one-frame 800x160 absolute run contained one invalid BUSY sample. The repeatable physical frames were approximately 17.07 FPS; the inflated aggregate that included the invalid sample is not a valid result.

The 40 MHz A/B mode produced repeated one- and two-microsecond BUSY intervals. Those were missed or unaccepted refreshes, not physical display updates. A general-use driver must stay at 20 MHz until a separate transport qualification proves otherwise.

### 2.2 What was not proven

v0.1.5 established feasibility and short-run visual quality on one known `0x68` device. It did not establish:

- panel lifetime under continuous absolute drive;
- safe cleanup cadence;
- behavior across panel lots or `0x69` hardware;
- operation across the panel's rated temperature range;
- behavior at low battery voltage or during power transients;
- recovery from reset, sleep, aborted refresh, or lost panel RAM;
- application concurrency and ordinary RiscRTE lifecycle behavior;
- grayscale semantics for arbitrary application content;
- long-run absence of charge trapping or permanent column/row defects.

Until those items are qualified, “stable v0.1.5” means a repeatable laboratory mechanism, not a lifetime-qualified production mode.

## 3. How this differs from the existing normal driver and raw FastEPD

### 3.1 Existing X4 normal path

The current UC8279 provider uses the controller as intended by the ordinary firmware model:

1. copy the application image into UC8279 RAM;
2. use DTM1 and DTM2 as old/new image planes;
3. select the built-in OTP waveform;
4. ask the UC8279 timing controller to execute the refresh;
5. wait for BUSY to complete.

The current provider bit-bangs panel bytes, sends the full 600-row controller stride for both planes, and reselects the OTP waveform after power-on. That path is conservative but spends substantial time in software transport and the manufacturer's long waveform.

### 3.2 v0.1.5 path

v0.1.5 still uses the UC8279 timing controller; it does not directly toggle the glass row and source lines. It improves the controller-managed path by:

- moving data with native ESP32-S3 hardware SPI at 20 MHz;
- sending only the ordinary partial window that actually changed;
- loading external LUT registers `0x20` through `0x24`;
- reducing the waveform to one or two scan frames;
- pre-rendering/caching the bytes before the timed transfer;
- using temporal Bayer phase changes to prevent residual errors from staying fixed to the same pixel lattice;
- optionally making the LUT depend only on the new target color, allowing the visible burst to skip the DTM1 synchronization upload.

### 3.3 Raw FastEPD approach

A raw FastEPD-style driver directly clocks panel source/gate data and controls row timing and waveform phases from the host. The X4 Pro places the UC8279 controller between the ESP32-S3 and the electrophoretic glass, so the host cannot reproduce that electrical architecture without bypassing the controller.

The v0.1.5 solution is therefore the X4 equivalent in philosophy, not in bus topology: it shortens and controls the UC8279's external waveform while keeping the controller responsible for gate scanning and high-voltage sequencing.

## 4. Why the laboratory firmware cannot simply replace the production driver

The lab firmware has several properties that are useful for measurement but unacceptable in a general driver:

- It generates a known synthetic pattern rather than consuming arbitrary application pixels.
- It precomputes an entire test sequence. The 800x160 runs allocate 33 frames at 16,000 bytes each, or 528,000 bytes, before starting.
- It performs an OTP full-clean baseline before every mode, hiding long-term accumulation that an application workload would expose.
- It has no renderer, app switch, sleep/wake, driver restart, or power-management concurrency.
- It can assume one active region and one controlled sequence.
- It does not maintain a durable distinction between the logical framebuffer, UC8279 DTM1, UC8279 DTM2, and the uncertain physical state of the glass.
- The absolute modes intentionally skip DTM1 synchronization and therefore leave the controller's old-image plane stale.
- It reports timing but does not implement production recovery, fallback, health accounting, or a panel-wear policy.
- Its temporal Bayer generator creates the test image itself. A production driver cannot indiscriminately dither arbitrary monochrome application output without changing the image.

The production conversion is primarily a state-management and policy project. Copying the LUT bytes and replacing `spi_byte()` is necessary but insufficient.

## 5. Required production architecture

The UC8279 implementation should be split into six explicit layers. The existing SSD1677 path should remain separate and unchanged.

### 5.1 Hardware SPI transport

Responsibilities:

- native ESP32-S3 SPI at a fixed, whitelisted 20 MHz;
- DMA-capable staging buffer in internal memory;
- row/window transfers from internal RAM or PSRAM;
- bounded transactions with scheduler yield points;
- immediate CS release and controller reset on fatal fault;
- byte and bulk-command helpers with consistent error propagation;
- transfer timing and byte counters.

The transport API should be controller-neutral enough that it can later consume a generic RiscRTE SPI capability. The first implementation may remain inside the board driver if that is the shortest integration path, but controller logic must not call GPIO bit-banging directly.

Production must not allocate an animation's worth of cached frames. It should retain only:

- the application-facing current framebuffer;
- one host shadow representing the last accepted target;
- one region-sized DMA/staging buffer;
- optional tile accounting and dither metadata.

### 5.2 UC8279 command engine

Responsibilities:

- controller reset and double-probe;
- strict admission by controller ID;
- normal 800x600 geometry and fixed 120-row visible offset;
- ordinary partial-window programming;
- power-on, refresh, power-off, and deep-sleep sequencing;
- external-LUT upload;
- DTM1/DTM2 writes;
- BUSY edge validation;
- recovery reset and OTP clean.

The command engine must whitelist register values. The production binary must contain no code path that can select:

- compact or remapped `TRES/GSST` geometry;
- reduced/zero TCON experiments;
- SPI above 20 MHz;
- PLL values other than the qualified set (`0x0E` and `0x0F` initially);
- arbitrary application-provided LUT bytes;
- voltage or PMIC experiments.

### 5.3 Panel-state model

The driver needs a state machine that explicitly tracks both controller RAM and physical confidence:

- `UNPROBED`: no controller identity or state is trusted;
- `UNKNOWN`: controller is known, but panel RAM/glass correspondence is not trusted;
- `CLEAN_SYNCED`: OTP clean completed and DTM1/DTM2 shadows are established;
- `DIFF_SYNCED`: DTM1 matches the driver's accepted old image and differential refresh is legal;
- `ABSOLUTE_BURST`: absolute target waveform is active and DTM1 is intentionally stale;
- `SETTLING`: final target is being reinforced and controller old-image state is being restored;
- `SLEEPING`: controller is in deep sleep and RAM must not be assumed preserved;
- `FAULTED`: no additional fast refresh is allowed until reset/recovery completes.

Required invariants:

1. Differential refresh is legal only in `CLEAN_SYNCED` or `DIFF_SYNCED`.
2. Entering absolute mode marks DTM1 stale immediately.
3. Leaving absolute mode requires a final target reinforcement when policy requests it, followed by a DTM1 upload of the accepted final target.
4. Any timeout, missed BUSY assertion, reset, or aborted SPI transaction marks panel state `UNKNOWN` or `FAULTED`.
5. Recovery from unknown state is controller reset, known initialization, OTP clean, DTM1/DTM2 reseed, then `CLEAN_SYNCED`.
6. Host framebuffer history and controller DTM1 validity are separate facts. The existing `seed_previous` callback cannot be treated as proof that panel RAM matches the glass.

### 5.4 Refresh-profile manager

The driver should expose behavior through policy profiles rather than raw waveform selection.

| Internal profile | Intended use | Waveform/state | Required follow-up |
|---|---|---|---|
| `CLEAN_OTP` | Boot recovery, explicit clean, severe ghost budget | Built-in OTP | Reseed DTM1 and DTM2 |
| `QUALITY_DIFF_2F` | Static text, final page, conservative partial | Two-frame differential | Synchronize DTM1 after refresh |
| `MOTION_DIFF_1F` | Short interactive motion where accurate old state is retained | One-frame differential | Synchronize DTM1 after every refresh |
| `BURST_ABS_1F` | Scrolling/animation burst | One-frame absolute, no DTM1 sync during burst | Bounded duration, then settle and DTM1 reseed |
| `SETTLE_ABS_2F` | Reinforce the final burst frame | Two-frame absolute final target | Upload final target to DTM1 |
| `RECOVERY_OTP` | Fault or uncertain state | Reset plus OTP clean | Re-enable fast profiles only after success |

`BURST_ABS_1F` must never be the unbounded default. Absolute drive energizes unchanged pixels on every frame and accumulates less-observable charge error. Its maximum duration, frame count, and cleanup thresholds must be configurable and established by endurance testing.

The first production implementation can select profiles internally from submission cadence:

- isolated/static present -> `QUALITY_DIFF_2F`;
- repeated damage submissions inside an interaction window -> `MOTION_DIFF_1F` or bounded `BURST_ABS_1F`;
- end of burst/idle deadline -> `SETTLE_ABS_2F` and DTM1 reseed;
- `RISC_DISPLAY_PRESENT_CLEAN` -> `CLEAN_OTP`.

A later display API revision may add explicit static/interactive/animation hints, but the driver must remain correct when no hint is provided.

### 5.5 Damage planner

The planner must accept ordinary `risc_display_rect_v1` damage and produce valid UC8279 windows:

- clip to the 800x480 visible framebuffer;
- align X and width to byte boundaries;
- retain normal controller geometry and add the fixed 120-row offset only when programming the UC8279 window;
- coalesce overlapping rectangles;
- choose one bounding window or several sequential windows using a measured cost model;
- fall back to full visible refresh when the union is large enough that window overhead no longer wins;
- preserve every pixel in the declared region; no benchmark-only region reduction.

A useful cost estimate is:

```text
estimated_present = upload_bytes / measured_spi_throughput
                  + expected_waveform_time(profile, row_count, temperature)
                  + command_overhead
                  + optional_dtm1_sync
```

The cost model should be learned from valid completed refreshes and bounded by conservative defaults.

### 5.6 Ghosting and charge-budget manager

Track health per tile rather than using only a global refresh count. A 32x16-pixel or similar tile grid is sufficient.

Each tile should record:

- W->B transition count;
- B->W transition count;
- net transition imbalance;
- absolute-drive exposure count;
- consecutive fast frames;
- time since two-frame settle;
- time and updates since OTP clean;
- confidence that physical state matches the target.

Policy actions:

- end every absolute burst with a qualified settle/reseed sequence;
- promote individual dirty regions to two-frame differential/absolute reinforcement;
- promote the whole display to OTP clean when global confidence or charge balance crosses a threshold;
- force clean before accepting critical static content if the state confidence is low;
- never let failed or invalid refreshes advance the accounting ledger.

Initial limits should be deliberately conservative and stored as tunables. Release values must come from endurance and optical measurement, not from the short v0.1.5 run.

## 6. Temporal Bayer integration

The temporal Bayer test result must not be misinterpreted as permission to modify every monochrome frame.

The current `display.output` provider advertises `MONO1`. For solid black and white application pixels, the driver should preserve the requested value exactly. Temporal Bayer phase rotation is appropriate only for pixels that represent grayscale/antialiasing or for a compositor-provided motion representation.

A reliable integration has three possible levels:

1. **MONO1-safe first release:** use the fast waveform for arbitrary monochrome damage, but do not invent gray pixels. Temporal phase metadata is used only where the renderer already supplied a dithered edge/gray pattern.
2. **Optional grayscale surface:** add `GRAY4` or `GRAY8` support and convert eligible damaged pixels through a deterministic four-phase Bayer sequence.
3. **Compositor phase contract:** allow the compositor to provide four pre-dithered MONO1 phases plus a final static phase.

For any grayscale path:

- phase must advance only after a valid physical refresh;
- a retry must repeat the same phase;
- final static content must use a deterministic phase;
- phase state must reset after OTP clean, controller reset, or unknown-state recovery;
- dithering must be confined to damaged regions and must not alter unrelated text or UI pixels.

## 7. BUSY validation and fault recovery

The v0.1.5 log demonstrated that “BUSY returned quickly” is not enough to prove that a refresh occurred. The driver must require an edge sequence:

1. BUSY_N is idle before `DRF`;
2. BUSY_N asserts within a profile-specific assertion deadline;
3. BUSY_N remains asserted longer than a conservative minimum physical duration;
4. BUSY_N deasserts before the completion deadline.

A one- or two-microsecond interval is invalid and must never be included in performance statistics or accepted as a displayed frame.

Recommended recovery:

- first invalid edge sequence: stop the present, mark state unknown, reset and re-probe;
- if probe and initialization succeed: OTP clean and reseed, then report the original present failed;
- repeated protocol failure in the same boot: disable all fast profiles and retain the OTP path only;
- repeated reset/probe failure: hold panel reset and fail the provider rather than continuing to issue commands.

The driver must yield to the scheduler during long transfers and BUSY waits. The early laboratory watchdog resets were software starvation, not panel failures, and production must retain watchdog coverage rather than disabling it.

## 8. Temperature and power qualification

A fixed one-frame waveform cannot be assumed to produce the same optical transition at every temperature. The production driver needs either a usable panel/board temperature source or a restricted operating policy.

Required behavior:

- record temperature with every profile decision and timing sample;
- qualify LUT/profile behavior in temperature bins across the intended operating range;
- disable one-frame absolute mode outside the qualified bins;
- lengthen the waveform or fall back to two-frame differential/OTP as temperature departs the validated range;
- re-evaluate the ghost budget more aggressively when cold;
- validate operation at battery-voltage and CPU-frequency boundaries used by the X4 firmware.

If no reliable temperature capability is available in the first release, fast mode should be limited to a measured room-temperature band and fall back outside it.

## 9. Power, sleep, and lifecycle behavior

The driver must integrate with the existing provider lifecycle:

- `start`: probe, initialize, establish unknown state, then perform or defer an OTP clean before the first present;
- `acquire`: return the normal MONO1 surface without exposing waveform details;
- `submit`: validate damage, classify intent/cadence, and queue a profile decision;
- `wait_present`: execute transport, waveform, BUSY validation, state transition, and accounting;
- `quiesce`: finish or reject an active present, settle an absolute burst, power off, enter deep sleep, hold reset, and mark RAM state unknown;
- wake/restart: re-probe and recover before any differential refresh;
- fault/abort: release CS, reset the controller, invalidate both panel-RAM assumptions, and require recovery.

Keeping the controller powered during a short interaction burst avoids repeated power-on overhead. It must be powered off after the idle/settle deadline and before system sleep.

## 10. Integration with the existing `x4pro-panel` provider

The existing provider already supplies the correct external capability shape:

- `display.output` API v1;
- `get_info`;
- `acquire` / `release`;
- `submit`;
- `present_status` / `wait_present`;
- `seed_previous` history hook;
- `quiesce` and diagnostics.

The conversion should preserve that API and replace only the UC8279 implementation behind it.

Recommended file split:

```text
Drivers/x4pro_panel/
  driver.c                    API adapter; controller selection; SSD path
  uc8279_engine.c/.h          command, power, LUT, RAM and BUSY state machine
  uc8279_profiles.c/.h        qualified waveform tables and profile policy
  uc8279_damage.c/.h          rectangle normalization and cost planner
  uc8279_health.c/.h          tile charge/ghost accounting
  x4_spi_transport.c/.h       20-MHz DMA transport
  manifest.json
```

Specific changes to the current provider:

1. Replace `spi_byte()` use in the UC8279 path with bulk hardware SPI.
2. Stop transferring both complete 800x600 planes for ordinary partial damage.
3. Add external-LUT profile upload instead of always selecting OTP after PON.
4. Preserve the SSD1677 path separately.
5. Separate application history from DTM1/panel-state validity.
6. Make successful present completion contingent on valid BUSY edges.
7. Add fast-profile, fallback, retry, cleanup, temperature, and health diagnostics.
8. Update reported latency only after the default profile has been qualified; expose profile-specific timing through diagnostics because one static latency cannot describe every mode.
9. Increment the driver as a new minor architecture version, proposed `0.2.0`, while retaining experimental/unpublished status until qualification passes.

## 11. Diagnostics required for general use

Every present should make the following fields available through the existing diagnostics path or an auxiliary statistics capability:

- controller ID and probe result;
- selected profile and reason;
- damage rectangle(s), normalized window, and bytes transferred;
- SPI rate;
- DTM2 upload time;
- waveform BUSY assertion and completion timestamps;
- DTM1 synchronization time;
- total present latency;
- physical-refresh validity result;
- retry/recovery count;
- current panel-state enum;
- absolute-burst frame count and duration;
- tile/global ghost budget;
- clean and settle counts;
- temperature and power state;
- fast-mode disable/fallback reason.

Do not report calculated FPS for invalid refreshes. Do not erase the original failure when recovery succeeds; report both the failed present and the recovery action.

## 12. Qualification program

### 12.1 Functional matrix

Test at minimum:

- all four logical transitions: WW, WB, BW, BB;
- solid black, solid white, checkerboard, fine text, antialiased text, gray ramps, photographs, and rapidly reversing scroll patterns;
- damage at top, bottom, left, right, and all byte-alignment boundaries;
- 8-pixel-wide through full-screen windows;
- overlapping and disjoint rectangles;
- profile transitions in every direction;
- end-of-burst settle and DTM1 reseed;
- explicit clean requests;
- app exit during a burst;
- sleep/wake, reboot, driver restart, and power interruption;
- abort during upload, during BUSY assertion, and during waveform execution;
- stuck-low, stuck-high, delayed, and implausibly short BUSY fault injection;
- PSRAM pressure and DMA allocation failure;
- low-battery and reduced-CPU-frequency operation.

### 12.2 Panel population

- Treat `0x68` and `0x69` as separate qualifications.
- Initial release should enable only the exact controller/LUT IDs that have passed.
- Use multiple units from more than one purchase lot. A useful minimum is five `0x68` panels for functional/endurance release testing, with additional sacrificial units for destructive-limit work.
- A single passing device is not evidence of production reliability.

### 12.3 Endurance stages

The following are proposed engineering gates, not previously achieved results:

1. **Bring-up:** 10,000 mixed fast updates on a sacrificial panel with periodic optical captures.
2. **Qualification:** at least 1,000,000 mixed updates per release-candidate panel across static, scroll, gray, and polarity-reversal workloads.
3. **Accelerated wear:** at least one sacrificial panel driven to 10,000,000 mixed updates or until a measurable degradation limit is reached.
4. **Lifecycle:** at least 1,000 sleep/wake and controller-recovery cycles.
5. **Soak:** multi-day interaction/idle cycling with realistic cleanup policy rather than a clean before every mode.

After each stage, run the ordinary OTP clean and inspect for persistent gray blocks, column/row defects, contrast loss, edge retention, and charge bias after a rest period.

### 12.4 Optical acceptance

Build a fixed camera/lighting jig and record:

- white and black reflectance;
- transition response time;
- ghost image amplitude after a standard sequence;
- uniformity by row/column;
- residual image immediately after clean and after a defined rest period;
- change from each panel's pre-test baseline.

Release thresholds should be numerical and checked automatically. Any persistent block, row, or column visible after the normal clean sequence is an immediate stop condition.

## 13. Production safeguards

The release driver must include all of the following:

- compile-time and runtime register whitelists;
- 20 MHz SPI cap;
- PLL whitelist;
- fixed normal `TRES/GSST` geometry;
- no TCON experiments;
- double-probe controller admission;
- bounded absolute-burst duration;
- mandatory settle/reseed path;
- ghost/charge accounting;
- profile-specific BUSY minimum and maximum durations;
- reset-and-clean recovery;
- session-level fast-mode kill switch after repeated faults;
- OTP-only fallback that remains usable if the fast engine is disabled;
- no application API for arbitrary LUT or voltage values;
- clear version/provenance in diagnostics;
- v0.1.6 quarantine warning retained in repository documentation and CI artifacts.

A persistent fast-mode disable flag may be added once the driver has a suitable storage capability. Until then, disable fast mode for the remainder of the boot after repeated protocol failures.

## 14. Implementation sequence

### Phase 0 — preserve evidence and boundaries

- Keep `stable/x4-high-fps-v0.1.5` immutable.
- Keep v0.1.6 only on the quarantine branch.
- Add unit tests that reject every quarantined register/rate combination.

### Phase 1 — transport and engine extraction

- Extract the 20 MHz SPI, normal-window, external-LUT and BUSY code from v0.1.5.
- Build it as reusable UC8279 modules under `Drivers/x4pro_panel`.
- Retain the standalone lab as a comparison harness.

### Phase 2 — arbitrary framebuffer and damage support

- Replace procedural test-pattern generation with MONO1 framebuffer windows.
- Implement alignment, clipping, coalescing and the cost model.
- Validate byte-for-byte output against the existing driver for ordinary black/white frames.

### Phase 3 — state and lifecycle correctness

- Implement panel-state enum and invariants.
- Separate host history, DTM1 validity, DTM2 target and glass confidence.
- Implement clean, differential, absolute burst, settle, sleep and recovery transitions.

### Phase 4 — policy and health

- Add profile selection from intent/cadence.
- Add tile health accounting and conservative limits.
- Add temperature gating and OTP fallback.

### Phase 5 — RiscRTE integration

- Wire the engine into the existing provider API.
- Preserve SSD1677 behavior.
- Expand diagnostics and build tests.
- Publish only as experimental until qualification succeeds.

### Phase 6 — hardware qualification

- Run the functional, population, endurance, lifecycle and optical programs.
- Tune burst and cleanup thresholds from measured results.
- Promote `0.2.x` only after all release gates pass without persistent defects.

## 15. Definition of done

The v0.1.5 mechanism is a reliable general-use driver only when:

1. arbitrary RiscRTE MONO1 frames and damage rectangles render correctly;
2. the driver automatically selects, exits and recovers fast profiles without application involvement;
3. DTM1/DTM2 and glass-confidence state remain correct across every lifecycle transition;
4. false BUSY completions cannot be counted as successful presents;
5. faults fall back to a usable OTP path;
6. temperature and power boundaries are enforced;
7. long-run ghosting is bounded by an evidence-based cleanup policy;
8. multiple panels and lots pass the endurance and optical gates;
9. no persistent row, column, or block defects appear;
10. all v0.1.6 destructive mechanisms are unreachable in production code;
11. diagnostics are sufficient to reconstruct every present and recovery decision;
12. the driver remains compatible with normal RiscRTE app, sleep and release workflows.

Until those conditions are met, v0.1.5 remains the stable engineering baseline and the fast production path remains experimental.
