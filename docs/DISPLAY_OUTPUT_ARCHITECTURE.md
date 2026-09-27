# RiscRTE Display Output and Adaptive UI Architecture

## Status

This document defines the display migration foundation implemented in firmware
1.3.13. It follows the hardware ownership contract: the target physical display
implementation is an independently installable provider ELF. The compatibility
`HalDisplay` backend remains compiled into firmware only during the staged
migration and is explicitly **CURRENT/LEGACY, NONCOMPLIANT** with the final
hardware boundary.

## Layering

```text
application ELF
  -> RiscRTE UI service / adaptive layout
  -> GfxRenderer or future format-aware graphics service
  -> DisplaySurface compatibility boundary
  -> CURRENT: HalDisplay firmware backend
  -> TARGET: display.primary -> display.output provider ELF
  -> panel
```

UI code owns semantic layout. Graphics code owns rasterization. The display
provider owns pixels-to-panel transfer, panel buses, DMA, waveforms, refresh
timing, brightness/backlight and display-specific power sequencing.

## Provider ABI

`sdk/driver/RiscDisplayOutputV1.h` defines `display.output` API v1. Consumers
normally bind the logical alias `display.primary`. The ABI reports runtime
geometry, safe insets, physical dimensions when known, supported pixel formats,
rotation support, damage alignment, presentation capabilities and timing hints.

Supported initial format identifiers are MONO1, GRAY2, GRAY4, GRAY8 and RGB565.
No application is required to assume a particular format.

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

1. Replace the compatibility `HalDisplay` implementation with a real
   `display.output` provider ELF and bind it as `display.primary`.
2. Move the optimized GameBoy e-paper scan engine into that provider rather than
   replacing it with per-pixel host calls.
3. Add a format-aware graphics path capable of writing RGB565 and other provider
   formats.
4. Remove GameBoy's fixed 540x960 layout and use the same viewport contract.
5. Validate the same app ELFs against an LCD provider with a materially different
   resolution and aspect ratio.


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
