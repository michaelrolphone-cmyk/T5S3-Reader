# Model Viewer 1.2.2: faster, exclusive controller gestures

This app-only follow-up supersedes the controller-rate section of
`MODEL_VIEWER_INPUT_AND_CATALOG_FRESHNESS.md`. Catalog/firmware behavior from
that document is unchanged. Model Viewer advances from 1.2.1 to 1.2.2;
minimum firmware for that controller-only update was 1.3.18.
The combined DSP integration is Model Viewer 1.2.3 and requires firmware
1.3.29; all controller changes documented here are preserved.

## Rates and controls

Relative to 1.2.1, yaw/pitch rotation is doubled (1.95 -> 3.90 radians/second),
pan is eight times faster (360 -> 2880 logical pixels/second), and proportional
zoom is eight times faster (2.25 -> 18.0 per second in log scale). Hold A for
one quarter of the selected rate. The D-pad rotates; LB+Up/Down zooms;
RB+D-pad pans. Diagonal pan is normalized and RB wins when both bumpers are held.
X exits the viewer in HID and XInput modes. A remains the precision modifier
and neither A nor B exits. Touch, canvas double-tap reset and semantic Back
(physical Back / keyboard Escape) are unchanged. A alone does not reset.

The existing 120 ms start ramp, 48 ms pending-time cap and camera bounds remain.
These are camera rates, not a renderer FPS increase. Slow mesh rendering may
still limit visible throughput. The zoom polynomial now handles the complete
new bounded exponent range (+/-0.864), rather than extending the old small-step
approximation outside its intended range. The app still needs no libm import.

## Remaining rotation entry path addressed

The 1.2.1 mapped-input filter prevents duplicate compatibility directions from
being applied alongside raw gamepad input. However, its raw motion selector
itself is stateless: after LB+Up or RB+Up, an Up-only snapshot selects rotation.
Releasing a bumper before the D-pad, or losing its bit in an intermediate
snapshot, can therefore turn the end of a zoom/pan into a rotation even though
mapped-input filtering is working. This is an executable failure path, not
proof of the particular USB reports produced by the owner's controller.

The motion policy now remembers that a bumper gesture was admitted. While its
bumper is absent, remaining D-pad input pauses instead of rotating. It does not
replay previous movement or keep panning after release. If the same chord
returns, its completed start ramp is preserved but no time from the pause is
integrated. An explicit LB/RB mode change is still allowed immediately.

To return from pan/zoom to unmodified rotation, release the D-pad and bumpers
until neutral has been observed for 48 ms, then press a direction. A may stay
held. A brief fully-neutral snapshot does not cancel the guard. The timer is
unsigned-wrap-safe and works in both the main input handler and render-service
calls to the existing motion helper. This state is per app invocation, with
no event queue, hardware lifecycle changes or dynamic allocation.

## Tests and limits

The existing `model_viewer_controls_test.py` now checks the exact 2x/8x rates,
quarter-speed precision, the higher-rate zoom approximation, both release
orders, absent-bumper snapshots, changing D-pad direction while paused, neutral
blips, resume without a repeated start ramp, LB/RB switches and timer rollover.
The gesture matrix uses both HID/XInput mappings and fine/normal speed, and
injects duplicate mapped camera input into the unmodified app input handler and
render checkpoint. Prior provider, ownership, reconnect, bounds and Back tests
remain; the old test expecting immediate rotation after a pan release now
requires a neutral transition. The existing CI runner already executes this
suite with AddressSanitizer and UndefinedBehaviorSanitizer.

Targeted local runs used the fetched production header, exact gamepad/capability
ABI definitions and the unchanged app helper excerpts. Platform I/O is mocked;
a local-only include shim replaces unused USB controller declarations. These
runs do not build the whole application or validate actual USB/e-paper behavior.
No local fixture/shim or partial app source is committed. Full repository and
Xtensa builds are separate CI checks. The original device reports were not
captured, so hardware-specific causes beyond the demonstrated transition remain
unverified.
