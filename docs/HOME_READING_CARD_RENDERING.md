# Home reading card rendering

The single-book Home card uses four real display tones. `ReadingCardStyle`
contains its clipped layout and tone raster; `HomeReadingCard` composes the art
and metadata and presents the final Home frame. Multi-cover themes retain their
existing renderer.

The first version used a 4x4 Bayer pattern and an abrupt quarter-height density
change. That was a visual approximation, not equivalent to the true-grayscale
approach in PR #238. The current version removes that pattern. A solid black
face is surrounded by a dark outer lip, a thin reflective rim that steps through
white/light gray/dark gray from the upper-left and lower-right highlights, and
a black recessed edge. A narrow curved dark-gray reflection sits beneath the
top lip. Rounded corners have a 36px radius at the usual Home dimensions.

The clock-widget reference has a subtler rim than the app-icon frames in #238;
this card intentionally does not copy their thick, uniformly bright perimeter.
With four physical tones, reflections are quantized, not continuous gradients.
Physical panel contrast and ghosting still require device observation.

## Frame ownership and presentation

1. Draw the normal Home BW base, including white text and uninverted cover art.
   Gray chrome pixels have black base bits. Cache this unfocused base as before.
2. Draw focus, header/menu/hints through the ordinary Home render flow.
3. Save the complete BW frame and capture the compositor's base plane.
4. Use the renderer buffer as scratch for LSB and MSB chrome component planes.
   Zero the entire scratch plane first. Dark gray sets an LSB bit; light gray
   sets an MSB bit. The real HalDisplay compositor lets white base pixels win.
5. Restore the complete BW frame **before** presentation. Check that all three
   compositor buffers exist and that the base capture is valid. Use a single
   `displayGrayBuffer(HALF_REFRESH)` call, or one ordinary BW presentation if a
   snapshot/base/component allocation failed. Failures are logged.

No panel power, initialization, transport, waveform tables, or physical driver
implementation changes are required. The sole shared-display addition is an
inline, read-only buffer-readiness query. Gray buffers use the existing backend
allocation path; the temporary snapshot uses the renderer's existing BW storage.
This is not the old monochrome FAST_REFRESH path and has its grayscale refresh
cost; there is no claim of unchanged display latency.

Gray reflection pixels stay outside the 24px content inset. Thus black cover
pixels remain black as well as white pixels staying white. Art is not reread
for the two gray passes or for a valid cache hit. A short uncached white focus
mark replaces the full white selection contour so selection does not obscure
the directional rim.

## Reproducible host checks

Run `python3 test/home_reading_card_test.py` from the repository root. It compiles
the production card and tone raster, decodes actual packed BW/LSB/MSB test frames
with HalDisplay's tone truth table, and checks pixels, not just draw-call counts.
It covers frame restoration, preservation of header/menu/text/art, repeated
presentation and cached selection changes, all four allocation-failure stages
and recovery, clipped/tiny slots, aspect ratios, missing covers, and metadata.
The new readiness accessors compile from the real headers under both board
configuration macros. These are host tests, not hardware emulation.

For a chrome-only raster of the production shader:

```sh
g++ -std=c++17 -O2 -Isrc test/reading_card/render_chrome.cpp -o /tmp/reading-card
/tmp/reading-card > /tmp/reading-card.pgm
```

This PGM shows geometry and the four digital gray levels; it does not simulate
fonts, book art, e-paper reflectance, refresh speed, or ghosting.
