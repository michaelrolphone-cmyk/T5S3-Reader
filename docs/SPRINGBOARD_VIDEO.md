# Springboard fast-video paging

Springboard 1.3.0 uses the existing fast EPD video API on T5S3 hardware with
firmware 1.3.39. It keeps the existing Font Awesome icons, font rendering,
grayscale tiles, labels, Home pinning, page dots and keyboard navigation.
Unsupported boards keep the ordinary renderer. The app's firmware minimum
prevents installing it against a host without the new raster/contact entries.

The loader's explicit DISPLAY | UI_VIDEO request preserves firmware touch
capture while relinquishing the physical display. Existing display-takeover
apps retain their original touch suspension. Software drawing into the retained
UI framebuffer is allowed in UI_VIDEO mode; physical present calls/dialogs are
not. The video service stops before app memory reclamation and host restoration,
using the existing failed-teardown protections.

Two append-only host primitives provide a copied 2bpp UI frame and a
non-consuming current touch contact (including movement). Raster snapshots use
the same grayscale precedence as ordinary presentation and apply UI flip.
The snapshot exposes no firmware buffer pointer. The app uses this software
raster service to preserve existing font/icon behavior, then submits pixels
through T5VideoApi; no peripheral protocol is added to the app.

The current and neighboring pages occupy three fixed 129,600-byte PSRAM buffers.
Fonts and app metadata are rasterized when caches change, never per animation
frame. A horizontal drag locks after 12 pixels and follows the current contact;
vertical movement is rejected. Release commits at one fifth of screen width,
or on a short flick of at least 80 pixels. Incomplete/cancelled gestures snap
back. Completed swipes missed between polls still page normally. Dragged icons
cannot launch or toggle pins. Page order retains the existing cyclic behavior.

Settling uses a 180 ms cubic ease-out. Frames are submitted at most every 40 ms
and only when the driver's backbuffer is available; obsolete positions are
skipped rather than queued. Offsets are quantized to four pixels for contiguous
packed-pixel copies, including landscape. Top controls and bottom page dots
remain stationary during movement. All four orientations and UI flip are
supported. Exit, failed allocation/snapshot, or a 2.5-second submission stall
uses the same bounded video/cache cleanup.

Tests execute actual animation code for drag/settle/cancel/vertical lock,
backpressure and cleanup, compare every composited pixel against an independent
rotation reference, verify gray packing/flip, and exercise actual hardware
handoff functions for preserved touch and unsafe-teardown retention. Existing
springboard input/pinning and idle/input regressions remain in CI. The Xtensa
ELF and its versioned sidecar are built and structurally validated.

The intended submission cadence is not a measured physical refresh guarantee.
Panel response, gray settling and ghosting still need evaluation on the device;
this does not claim LCD-style 60 fps behavior.
