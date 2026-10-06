# Built-in font style prewarm regression

The recording layer used to concatenate all draw calls for a font ID, then
prepare that union separately in every requested style. It now records only
the text requested for each actual built-in font data pointer. Style fallbacks
and explicit aliases within a family share their text and one page slot.
SD-font aggregation, text order, style counts and its preparation call stay unchanged.
No glyph/slot limits, decompressor, layout or raster algorithm changes.

Run `python3 test/font_style_prewarm/prewarm_test.py --enforce-cost`, adding
`--sanitize` for ASan/UBSan. The existing Springboard aggregate runs the
sanitized cost regression. `--baseline-ref <commit>` builds the old cache
manager and header against the same real font/decompression/raster harness;
adding `--enforce-cost` must fail on the unfixed original.

Measured on source baseline `36891e71ab651152c618d60d1d248abb4a7f44ff`, using
checked-in Noto Sans 12-point regular/bold/italic/bold-italic assets:

| Unique regular characters + 3 styled glyphs | Original cache payload | Repaired | Original group decodes | Repaired |
| --- | ---: | ---: | ---: | ---: |
| 64 | 17,748 bytes | 4,422 bytes | 4 | 4 |
| 128 | 36,124 bytes | 8,567 bytes | 12 | 6 |
| 256 | 80,536 bytes | 18,765 bytes | 16 | 7 |

Payload counts include page bitmap buffers and glyph lookup arrays. They
exclude text strings, temporary groups, vector/allocator overhead and peak
heap. Group counts execute the actual production inflater. These are host
operation/allocation counts, not physical-device timings or a latency claim.

The test compiles the complete production cache manager, font family, font,
UTF-8, decompressor, InflateReader and uzlib implementations. It extracts
unchanged production drawText, rotated text, glyph painting, drawPixel and
orientation bodies. The host framebuffer/device setup, logging/time and
getGlyphBitmap dispatch are fixtures. The SD fixture checks the unchanged
text/mask/call/lifecycle contract; it is not a filesystem or SD benchmark.

Cases cover normal/mixed styles and repeated bodies, actual fallback/alias
identity, all four orientations and BW/both grayscale planes, unsupported
glyph fallback, underline masking, ligatures/chaining, combining marks,
multiple font IDs, more than four font identities, more than512 glyphs,
each prewarm malloc failure, one decompression failure, next-page retry,
repeated end/clear, empty/null/unknown font inputs, moved scopes and
destructor-only cleanup. Assertions compare full framebuffer vectors and
requested glyph bytes with direct-prewarm and/or uncached production controls.
Allocation wrappers verify ownership and zero retained page allocations.

Local sanitizer execution may require `ASAN_OPTIONS=detect_leaks=0` under
ptrace. This does not disable ASan/UBSan or the explicit allocation-ownership
checks. No hardware display, target heap peak or hardware timing is claimed.
