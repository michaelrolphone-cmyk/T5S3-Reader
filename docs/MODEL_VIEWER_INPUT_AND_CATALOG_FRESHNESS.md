# Model Viewer input follow-up and catalog freshness

## Versions and scope

Model Viewer `1.2.0 -> 1.2.1` changes only the app's camera-input policy and help text.
Its minimum firmware remains `1.3.18`. Firmware `1.3.22 -> 1.3.23` contains the
shared catalog HTTP freshness change. The controller fix does not depend on the
new firmware; the catalog fix does. No driver, renderer, display capability,
release workflow, published asset or live release-index content is changed.

## Controller behavior

D-pad Left/Right changes yaw; Up/Down changes pitch. LB+Up/Down zooms in/out;
RB+D-pad pans, including speed-normalized diagonals. RB retains priority when
both bumpers are down. LB+Left/Right does not rotate.

Normal rates are now 1.95 radians/second for rotation, 360 logical pixels/second
for pan, and 2.25/second for logarithmic zoom: exactly three times the 1.2.0 rates.
Holding A multiplies any of these by 0.25. Pressing or releasing A during movement
preserves the current view and the already-completed start ramp, discarding only
pending old-speed time. A alone has no reset action, on press or release.
Double-tapping the canvas still resets the view. Back still exits.

The viewer previously applied mapped UI direction events as 0.012-radian
rotations after applying raw-controller pan/zoom. These sources could therefore
move two camera properties in the same update. A mapped direction also aborted
interactive rendering. The revised app arbitrates both its input handler and
render checkpoints: a connected raw pad owns camera input, including during
neutral/rearm or uncertain provider snapshots. It never applies the source-less
mapped camera events alongside raw input. After disconnect it drains to neutral
before keyboard/physical camera fallback resumes. Mapped Back and exit are never
masked; touch remains independent. Because the mapped API has no source identity,
keyboard/physical *camera* actions are fallback controls while no pad is present,
not concurrent camera controls alongside a connected raw pad.

The existing 120 ms start ramp, 48 ms maximum pending motion and current-state
polling remain. No buffered gamepad-event queue or frame-rate increase is claimed.
Expensive meshes may remain renderer-limited; delayed frames cannot accumulate
unbounded catch-up motion.

## Catalog investigation and repair direction

PR #249 merged as `5258a9fcc72c15f4c06b9270aae76578d48b6d26`. Release run
`36359640978`, publish job `108734322875`, created `app-model_viewer-v1.2.0` and
pushed index commit `dab20baf` at 23:44:56 UTC on September 27, 2026. The fetched
canonical index contains Model Viewer 1.2.0, size 22944, SHA-256
`25e21eff894c7fc0c785d11c66349b3606be9096b5e10cd3ff8feb18c49ca4aa`.

The App Store calls `app_catalog_refresh()` when opened. That clears its old
session catalog and fetches the same bare raw GitHub URL. No request revalidation
or cache-busting parameter was present. The owner's observation that reopening
showed the release is consistent with upstream caching/propagation, not evidence
of a failed index push. The stale response's headers were not captured, so its
precise cache origin/age is not asserted.

`ReleaseCatalogRequest.h` identifies only the mutable release-index and legacy
latest-catalog pointers. At the final HTTP transport (after native-stream
delegation), each such request gets a fresh random 64-bit query nonce plus
`Cache-Control: no-cache, no-store, max-age=0` and `Pragma: no-cache`. No RTC, device
identifier or repeated boot-time nonce is used. One metadata request remains one
metadata request; no API fan-out or response-body cache is added. Response Age,
X-Cache, ETag and Cache-Control are logged with bounded displayed lengths.

App Store, Driver Manager and the normal firmware-index check all use this
shared transport. Immutable release assets/sidecars, binary installation,
explicit HTTP credentials and unrelated URLs keep their existing behavior.
The firmware's separate legacy ESP-IDF fallback for `/releases/latest` is not
changed. Remote propagation and proxies that disregard request directives
cannot be proven absent by host tests.

## Verification

Run:

```sh
MV_SANITIZE=1 python3 test/native_apps/model_viewer_controls_test.py
MV_SANITIZE=1 python3 test/native_apps/model_viewer_shading_test.py
MV_SANITIZE=1 python3 test/native_apps/catalog_freshness_test.py
```

These compile the production helpers/request body with host substitutes only
for services and I/O. Coverage includes the exact 3x/quarter-speed rates, A
changes, HID and XInput mappings, duplicate mapped directions during pan/zoom,
render checkpoints, faults/reconnects/neutral handback, bounds, and the retained
shading/touch assertions. HTTP tests exercise direct and native-stream routing,
unique cache keys, request headers, failure handling and unchanged immutable
URLs. The complete repository build and physical hardware remain separate
checks; no device performance or live CDN trace is implied by these tests.
