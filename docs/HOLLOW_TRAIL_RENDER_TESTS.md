# Hollow Trail production renderer

Version 1.1.22 makes the device-tested nearest-camera experiment from 1.1.21
part of the default renderer, on top of the established mode 24 + mode 13 base.
The owner reported that only nearest camera improved FPS in the latest tests.
No numerical FPS gain is assumed.

Camera rotation and breathing now use one rounded nearest-neighbor source
sample per visible pixel instead of four bilinear taps. Camera movement,
framing, physics, AI background reconstruction, SIMD stages and dithering are
unchanged. Nearest sampling can produce sharper edges and visible pixel steps.
The transform is still skipped when the camera effect is inactive.

The unsuccessful alternate-background and motion-resolution experiments,
combined mode, pause-menu selector and optional 129,600-byte retained scenery
buffer are removed. Scenery updates every rendered frame at the existing
resolution. No mode selection is needed. The rolling ten-second FPS display
and pause/resume measurement reset remain available.

Validation uses golden frames captured from the 1.1.21 nearest-camera mode
across all chapters and scalar/SIMD readiness masks, an independent rounded
coordinate oracle, and grotto reference coverage. The native regression suite
also covers controls, pipeline ownership/fallback/cleanup and cooperative
render servicing. The ESP32-S3 build checks the actual distributable ELF;
source, sidecar and catalog versions and the ELF digest must agree.
