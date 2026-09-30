# Game display modes

Hollow Trail 1.0.4 and Risc Strike 1.0.2 default to fixed monochrome dithering
through the existing 1bpp fast-video path. This follows the owner's rejection of
the slow, flashing erase/redraw grayscale approach and approval to try the
GameBoy-style binary endpoint approach throughout gameplay.

## Selecting a mode

- Hollow Trail: pause with Start / Down, then A / Confirm (or Up) changes display.
- Risc Strike: Left / Right or controller X changes display on the title screen.
  During play, pause with Start, then A / Confirm changes display.
- Both show the selected mode. Every launch starts with dots; the choice is
  session-local. Gameplay state survives manual switching.

A manual switch stops and restarts the video service, validating its dimensions,
stride, format and polarity. That explicit switch can visibly reset the panel.
There is no automatic movement/idle switching, recurring clear, or temporal
alternation of patterns. Startup failure exits cleanly; no frame is packed into
a surface with an unsupported layout.

## Rendering

`Apps/epd_dither.h` maps darkness to a fixed 8×8 ordered threshold matrix at
physical 960×540 screen coordinates. Density approximates darkness to within
one dot per 64-pixel tile; white and black stay solid. The pattern does not depend
on time, frame number or camera position. Moving geometry can still change dot
coverage; fixed phase does not guarantee zero visible shimmer on a physical EPD.

Hollow Trail retains its layered scene, cached depth blur, radial fog, lighting,
bilinear enlargement and sharp character/UI. It quantizes the full 8-bit image
directly to dots, avoiding an intermediate four-shade loss. Risc Strike retains
wall-angle lighting and ground texture; the rasterizer represents the resulting
four tone values as dot densities. Future wall textures can still modify the
material before lighting and output quantization.

1bpp frames use 64,800 bytes rather than 129,600. No extra full-frame app buffer
or per-frame allocation is introduced. Risc Strike computes eight repeating
vertical dot values once per span, and one repeating byte per ground row.
Hollow Trail reuses the existing bilinear samples for either packing mode.
The mono scan path uses its existing endpoint pulse/history behavior; this
change does not claim one scan per settled image or a measured frame-rate gain.

Native grayscale remains available for direct comparison using the shared gray
transition changes in firmware 1.3.29. It is not selected automatically.

## Verification and limits

Host tests verify solid endpoints, monotonic dot density, fixed phase, full-frame
bilinear packing, and pixel-by-pixel agreement between Risc Strike's final gray
lighting/material image and its monochrome rasterization across three views.
They cover format rejection and paused mode selection. Existing Hollow Trail
route, pit, checkpoint, cache and render-service tests still apply.

Host previews preserve the forest silhouettes and depth but show visible grain.
Actual ghosting, optical brightness, gray-mode fidelity and frame rate require
hardware observation. The GameBoy result is encouraging evidence, not proof
that these larger moving scenes have identical panel behavior.

## Shared idle cleanup

Firmware 1.3.31 adds [bounded idle endpoint reinforcement](FAST_VIDEO_IDLE_CLEANUP.md)
to the shared fast-video backend. After 500ms without changed pixel targets,
small interleaved groups receive up to two extra endpoint pulses. There is no
full-screen clear or automatic mode switch, and new frames retain priority.
Dithered images are eligible throughout; native intermediate grays are left
unchanged to avoid shifting their shade. Physical effectiveness requires device
measurement.
