# RiscRTE Display Output and Adaptive UI Architecture

## Status

Firmware 1.3.47 retains `HalDisplay` for its own UI. Installable apps bind an
independently installable `display.output` provider. The first T5S3 provider,
`display-epd-video@0.1.2`, owns the app-facing display contract and temporarily
uses the firmware video scan service as its private physical backend. The host
UI compatibility backend is still compiled into firmware during migration.

## Master integration (September 30, 2026)

Backmerged master `1ebf0138`. Preserves asynchronous boot/loading and wisp
transitions, controller and math changes, inherited video backlight, and the
off-screen font renderer. Detached text targets initialize their own validated
MONO1 geometry. Firmware advances 1.3.46 → 1.3.47; Model Viewer 1.2.4 →
1.2.5; Risc Strike 1.0.2 → 1.0.3; display provider 0.1.1 → 0.1.2.

## Layering

```text
application ELF
  -> RiscRTE UI service / adaptive layout
  -> GfxRenderer or future format-aware graphics service
  -> DisplaySurface compatibility boundary
  -> CURRENT: HalDisplay firmware backend
  -> app ELF: display.output provider ELF
  -> panel
```

UI code owns semantic layout. Graphics code owns rasterization. The display
provider owns pixels-to-panel transfer, panel buses, DMA, waveforms, refresh
timing, brightness/backlight and display-specific power sequencing.

## Provider ABI

`sdk/driver/RiscDisplayOutputV1.h` defines `display.output` API v1. Consumers
bind `display.output`; the `display.primary` alias is reserved and is not yet
resolved by the generic provider loader. The ABI reports runtime
geometry, safe insets, physical dimensions when known, supported pixel formats,
rotation support, damage alignment, presentation capabilities and timing hints.

Supported initial format identifiers are MONO1, GRAY2, GRAY4, GRAY8 and RGB565.
Model Viewer and GameBoy require the T5S3's 960×540 MONO1 surface.
Risc Strike negotiates MONO1 for dots or GRAY2 for grayscale, preserving its
Start-button display toggle. The provider joins the previous scan before
changing format; consumers use the actual acquired stride and buffer size. An incompatible driver fails their display
initialization without writing to the panel.

Presentation uses intent rather than panel terminology:

- DEFAULT: provider policy.
- LOW_LATENCY: prioritize newest-frame latency.
- QUALITY: prioritize image quality.
- CLEAN: restore the best available panel state / clear accumulated artifacts.

FIFO and MAILBOX queue policies are generic. MAILBOX permits an obsolete queued
frame to be superseded by a newer frame. Submission is asynchronous and returns
a presentation token; polling/waiting is bounded. E-paper waveform names and LCD
vsync details do not cross the common ABI.

## Current compatibility surface

`DisplaySurface` is the in-firmware transition seam used by `GfxRenderer`.
`HalDisplay` implements it without changing current panel behavior. This makes
renderer geometry runtime data now, before the hardware backend moves to ELF.

The compatibility surface still contains the existing grayscale-plane helpers
used by the reader. Those helpers are not the provider ABI and must not become a
requirement for LCD/color providers. A future format-aware graphics layer should
render directly into a negotiated provider surface.

## Runtime geometry and safe area

`GfxRenderer` no longer initializes dimensions, stride or framebuffer size from
`HalDisplay::*` compile-time constants. `begin()` reads a
`DisplaySurfaceInfo` supplied by the active surface and currently rejects a
non-MONO1 surface until the format-aware rasterizer is added.

Safe insets are supplied by the display surface and rotate with logical
orientation. This removes panel-specific viewable margins from the renderer.

## Adaptive application UI

The append-only `T5UiApi` viewport query exposes logical width/height, safe
insets, content padding, vertical spacing, orientation and a compact/regular/
expanded size class. Existing apps remain ABI-compatible by checking
`struct_size` before accessing the new member.

The shared native UI renderer derives header, content, status and table/list
bounds from the runtime viewport instead of fixed 540x960-era offsets. App-owned
custom layouts should use the viewport query rather than fixed pixel origins.

## Migration continuation

The next display-specific milestones are deliberately outside this slice:

1. Replace the temporary provider-to-firmware scan backend with physical panel
   control inside the installable driver. Keep the public `display.output` ABI.
2. Bind the firmware UI compatibility renderer through the installed display
   provider and implement `display.primary` resolution.
3. Add a format-aware graphics path capable of writing RGB565 and other provider
   formats.
4. Remove GameBoy's fixed 540x960 layout and use the same viewport contract.
5. Validate the same app ELFs against an LCD provider with a materially different
   resolution and aspect ratio.

## T5S3 hardware smoke test

Use a firmware build from this PR at version 1.3.47. Install the complete
`display-epd-video@0.1.2` driver package, including `driver.elf`,
`provider-abi.v1`, `privileged-imports.v1` and `.package.json`, before opening
any of the migrated apps. Install Model Viewer 1.2.5, Risc Strike 1.0.3 and
GameBoy 1.3.9 from their respective PR artifacts, with their matching JSON
manifests. The GameBoy artifact is produced by its companion PR #25.

1. Confirm the Reader home UI starts and returns normally with the display
   provider installed; the firmware UI still uses its compatibility backend.
2. Open a model in Model Viewer. Rotate it, enable and disable the merged
   shading button, then exit. Reopen it to exercise a second handoff.
3. Open Risc Strike, draw a few frames, exit, then reopen. Confirm the host
   UI returns with its previous image intact.
4. Launch the GameBoy ELF, open a ROM, present several frames and exit. Confirm
   the host UI returns and the power button still responds.
5. Remove only the installed display provider and retry each app: mandatory
   capability gating should deny launch without taking over or clearing the
   firmware screen. Restore the provider before further app testing.

Record firmware/app/driver versions and the first provider/launcher diagnostic
on failure; this separates package discovery, display takeover, presentation
and teardown failures.


## Blank-screen safety invariants

Display bootstrap is deliberately fail-safe during this migration. These are
release invariants, not optional diagnostics:

- Runtime surface metadata is preflighted **before** the compatibility backend
  is initialized. Invalid width/height, stride, buffer size, safe insets or
  unsupported format cannot reach panel setup.
- The physical e-paper image is preserved while the compatibility backend and
  renderer bind are completed. Normal rendering only replaces it after the
  renderer accepts the surface.
- Current HalDisplay geometry is tied to BoardPins with compile-time assertions,
  and the entire canonical surface descriptor is compile-time validated. Legacy
  refresh-mode ordinals are also pinned so enum edits cannot silently select a
  different waveform.
- `GfxRenderer::begin()` is transactional. Invalid dimensions, stride, buffer
  size, safe insets, unsupported format, missing framebuffer, or a backend that
  is not ready returns failure without overwriting a previously working renderer
  state. Display metadata must never be enforced with an assertion/reboot loop.
- Surface validation is overflow-safe and bounded before any geometry is trusted.
- If the compatibility panel backend itself is alive but renderer validation
  fails, firmware draws a large renderer-independent emergency X plus an 8-bit
  diagnostic pattern (boot code `0xD1`) directly through `HalDisplay`. This
  path intentionally does not use runtime surface metadata or `GfxRenderer`.
- If the hardware backend cannot initialize, firmware does not clear the retained
  e-paper image and does not enter normal `ActivityManager` rendering.
- Once display bootstrap fails, the main loop is gated from normal UI rendering;
  it cannot repeatedly clear/present a broken surface.
- Runtime present calls against an unavailable T5S3 or EPD47 backend emit a
  one-time diagnostic instead of silently becoming a no-op.
- Returning from an exclusive display-takeover ELF reinitializes the host
  backend with panel preservation enabled. A failed GameBoy/host handoff cannot
  erase the last useful e-paper image before recovery diagnostics are available.
- The EPD47 logical framebuffer is initialized deterministically even when the
  physical panel image is being preserved.
- CI has both executable metadata-validation tests and source-contract checks
  for these invariants. A refactor that removes the guards must intentionally
  update those tests rather than silently weakening display recovery.


### Direct-render guard rule

Any `GfxRenderer` method that bypasses `drawPixel()` and calls a
`DisplaySurface` operation directly must fail closed until the renderer has
successfully completed transactional initialization. This includes bitmap/icon
blits, clears, present calls, page-turn requests and grayscale-plane operations.
The preflight path is the inverse rule: it may inspect only
`getSurfaceInfo()` and must never call readiness, framebuffer, drawing, power
or presentation operations because it executes before panel initialization.


### Retained-image boot boundary

The normal RiscRTE display bootstrap intentionally enters the compatibility
backend with `begin(false)`. CI treats that as a physical-output boundary:
the T5S3 path must use `initPreservingPanel()`, and neither supported backend
may clear, draw, push, or present the panel from that initialization path.
EPD47 also resets its software `displayReady` flag before every re-init so a
failed restart cannot inherit a stale ready state. Only after metadata preflight,
backend readiness, and transactional renderer binding succeed may normal UI
rendering replace the retained image.


### Current-board qualification pins

While `HalDisplay` remains as the compatibility backend, it is compile-time
qualified only for the currently shipped 4.7-inch e-paper profiles. Its physical
scan size (960×540), portrait logical size (540×960), MONO1 stride (120 bytes),
framebuffer size (64,800 bytes), and current safe insets are pinned with
`static_assert` checks. Changing those values is intentionally a build break.
A genuinely different resolution or panel must enter through the display
provider/profile architecture instead of weakening the legacy compatibility
backend.

The internal compatibility `DisplayPresentMode` ordinals remain the old
HalDisplay refresh ordinals and are deliberately *not* the same numeric values
as `RiscDisplayOutputV1` presentation intents. A future provider adapter must
map them explicitly rather than cast the enum numerically.
