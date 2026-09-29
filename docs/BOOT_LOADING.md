# Boot loading and display handoff

Firmware 1.3.40 starts the fast EPD animation worker before SD font discovery,
application state, OPDS/KOReader settings, installed-provider inventory and the
first navigation/touch admission. Startup work stays on the normal owner task;
the worker only composes/submits bounded video frames and never touches the SD,
module loader, ordinary renderer or its lock.

The existing stationary logo, left-to-right blocks, equal layer durations and
text fade are preserved when loading lasts long enough. The reveal now overlaps
real initialization. There is no mandatory 600 ms pulse and no requirement to
complete the reveal when startup has finished. Readiness cancels timed waits
and video backpressure waits. The optional exit fade has one 150 ms total
budget; the panel still requires its real transfer/ownership handoff time.

Home prepares its pinned menu and any missing recent-book thumbnails before its
first render. Boot thumbnail preparation does not display competing popups or
request intermediate renders. Reader resume keeps the animation during book
and font preparation. The activity renderer stops the worker and releases the
video owner immediately before its first destination render. Input dispatch
opens only after that render returns; completed/held loading-screen touch
gestures are discarded without unloading the touch provider.

DISPLAY | UI_VIDEO preserves the firmware touch subscription while video owns
the display. Cancellation uses an atomic stop request and completion handshake.
A one-second join timeout retains the video resources and retries the handoff;
it never forcibly deletes a task inside the driver or frees live buffers.
Failed hardware teardown likewise retains ownership. Recovery and panic paths
skip the animation and eager provider admission; desk-clock wake skips the
animation. Boards without fast video show one static loading frame without
multiple slow cosmetic refreshes.

Host tests compile the production startup implementation with platform doubles
and exercise early readiness, work progressing during reveal, full reveal,
backpressure cancellation, delayed worker acknowledgement, teardown retry,
idempotent completion and the ordinary-renderer fallback. Existing touch tests
exercise dropping loading-screen gestures and accepting the next fresh tap.
Device startup-time savings are not measured; the removed serial reveal/minimum
pulse waits and moved initialization are source- and test-verified.

## Touch focus across app launch

Native app launch previously reset controller focus but retained completed touch
queues. Old menu taps could therefore launch an icon in Springboard. Firmware
1.3.40 invalidates touch focus before the loading screen, once on the app's first
input access, and on return. It never flushes every poll.

The consumer clears its tap/swipe/Home queues and cancels held-gesture eligibility
immediately. Its existing capture task then polls the provider and takes an
authoritative snapshot as a sequence fence, so unread provider events also cannot
reappear as new app gestures. Failed polls/snapshots keep the fence pending for a
later bounded capture turn; no lease teardown, provider rescan, timer-based
suppression window or cross-task hardware access is introduced. New gestures are
accepted after that fence, and an existing contact must lift first.

Tests cover firmware-queued and raw-provider-queued old taps, held contacts,
snapshot failure/recovery, and the real GT911 driver/consumer together. A native
host test executes the first-poll code and verifies subsequent fresh taps are not
flushed. This is a firmware-only fix; Springboard remains 1.3.0.

## Cinematic ident (firmware 1.3.41)

The loading worker now renders the reveal at 40 ms intervals: incoming streaks,
outlined boxes filling left to right, subtle offset shadows, layered diamond
traces, moving edge lights and sparse particles. A narrow white highlight scans
only the logo interiors. The completed logo stays high contrast while a small
segmented activity rail and restrained density pulse continue during real work.
The rail is indeterminate and does not imply a percentage of startup completion.

The logo remains in its original centered position. All accents use bounded
integer vector drawing directly into the existing video backbuffer, with no new
allocations, assets, SD access, trigonometry or blur passes. Pulse work yields at
least 35 ms per frame. The existing readiness cancellation, 150 ms exit-fade
budget, touch boundary and resource-ownership handshake remain intact. Device
refresh quality and ghosting have not been measured. Host tests cover the full
visual cycle's buffer bounds and fully white fade endpoint as well as startup
lifecycle behavior.
