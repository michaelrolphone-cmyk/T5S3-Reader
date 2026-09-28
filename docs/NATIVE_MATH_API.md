# Native app math acceleration (firmware 1.3.29)

`T5MathApi.h` exports `t5_math_get_api(1)`, a size/version-checked CPU-only table.
It is a reusable runtime math primitive, not a display API or a peripheral
bridge. Apps do not import Espressif assembly symbols or own hardware. The
getter and each operation require the owning native-app task. No pointers are
retained, memory allocated, callbacks scheduled, or resources acquired.

| Operation | Contract | S3 implementation |
| --- | --- | --- |
| `add_s16`, `sub_s16` | Up to 2,048 elements; each mathematical result must fit int16; natural alignment; exact in-place allowed, partial output overlap rejected | Bundled `dsps_add_s16_aes3` / `dsps_sub_s16_aes3`, eight lanes, shift zero; scalar prefix/tail or incompatible alignment |
| `copy_bytes` | Up to 4,096 bytes; no overlap except identical pointers | Bundled `dsps_memcpy_aes3` for aligned whole vectors, libc otherwise |
| `fill_bytes` | Up to 4,096 bytes; arbitrary byte value | Bundled `dsps_memset_aes3` for aligned whole vectors, libc otherwise |

The original integer/memory operations accept null pointers for zero length. Invalid size, alignment, address-range
wrap, overlap, or task ownership returns false before modifying the output.
Buffers must be valid CPU-addressable RAM for the complete range, not MMIO.
Callers chunk larger work and yield; no call loops over an unbounded app length.
There is no hidden normalization, floating-point conversion, or rounding.
Signed result overflow is outside the vector API's specified domain.

## Why the wrapper is necessary

The installed Arduino/ESP-IDF SDK already contains `libespressif__esp-dsp.a`.
Its exact S3 add/sub object code was inspected, rather than assuming the latest
upstream implementation matches the bundled version:

- The SIMD entry checks input alignment but omits output alignment. The wrapper
  checks all three pointers and only calls that entry with 16-byte alignment,
  unit strides, a multiple of eight elements and shift zero.
- The fused load/add or load/sub instruction preloads an additional input vector
  after the last output. The wrapper leaves at least eight actual input elements
  for scalar cleanup; the preload never crosses the supplied array range.
- The bundled scalar assembly fallback is avoided. C handles incompatible
  alignment, prefixes and tails consistently for both operations.
- Only aligned whole-vector memory operations reach the assembly memory paths;
  this avoids their unaligned read-ahead and wide prefix accesses.

These guards deliberately stay inside the wrapper. Apps get a portable API and
cannot accidentally select an unsafe low-level entry point.

## Hollow Trail

The app uses 16-bit blur scratch and aligned sums. Blur sums are 0–4,845; even
adding a row before subtracting the outgoing row stays within 0–5,100. Thus the
16-bit operations preserve the exact previous integer result. The per-depth
blur radii, radial mix/fade and final 2bpp quantization are unchanged. Bulk plane
clears are chunked with cooperative checkpoints; the cached scroll operation
still uses memmove because its ranges overlap.

The getter was introduced in firmware 1.3.27. Hollow Trail 1.0.4 and
Model Viewer 1.2.2 require firmware 1.3.29 for the appended operations. Native ELF import validation
and launcher registration include the new symbol.

## Verification and limits

`test/native_apps/native_math_test.cpp` exercises alignment permutations, tails,
in-place operations, invalid bounds/overlap, task ownership, and renderer output
equivalence. Its host backend models the real assembly's extra preload under
ASan and checks all SIMD alignment/length preconditions. It does not execute S3
instructions. Firmware link/map inspection verifies the real bundled AES3
routines are present in the device build. Hardware measurements are still needed
to quantify the additional SIMD gain; host equivalence is not a device speed test.

## Batched operations added in 1.3.29

The v1 table grows by appending fields; its original field offsets and version
remain unchanged. Existing binaries keep using the prefix. Newly compiled apps
must check `struct_size` before reading an appended pointer, or require the new
firmware. The two updated apps require 1.3.29 and keep scalar rendering paths.

| Operation | Bound and semantics | Implementation |
| --- | --- | --- |
| `mul_s16` | 2,048 elements, exact signed product; caller guarantees every result fits int16; exact in-place allowed | Eight-lane AES3 with shift zero; same guarded prefix/tail as add/sub |
| `mat4_f32` | At most 64 rows: row-major `dst[N][4] = points[N][4] * matrix[4][4]` | AES3 for aligned buffers and N divisible by four; scalar otherwise |
| `dot_f32` | At most 2,048 components, one summed result | Aligned four-component groups use AES3; scalar tail |
| `dot4_f32` | At most 64 independent four-component products, one result per row | One bounded API call; AES3 four-component dot product per aligned row |

All float buffers need natural four-byte alignment, and float outputs cannot
overlap either input. Finite inputs and finite intermediate arithmetic are part
of the caller contract; NaN/infinity are not scanned for in the hot path. Float
operation order differs between optimized and scalar paths, so bit identity is
not guaranteed. Zero-length `dot_f32` requires a valid result and writes zero;
zero-row matrix/dot4 operations accept null pointers and do nothing. Invalid
bounds, ownership, pointer alignment/range or overlap fail before output writes.

The signed multiply wrapper never selects the bundled scalar assembly fallback:
its first sample adds rather than multiplies. The SIMD entry also omits output
alignment checks and preloads an extra vector. Both hazards are guarded as for
add/sub. No fixed-point shift/rounding variant is exposed; unshifted products
within int16 range avoid differing saturation and rounding conventions.

The matrix wrapper calls the concrete AES3 symbol only under its verified shape
conditions, avoiding an installed 3x3 fallback macro that incorrectly aliases
output to the second input. Matrix and float dot code use 128-bit loads, but
float multiply-accumulate instructions are scalar, not four-lane float SIMD.

### Consumers

Hollow Trail blends near/wide masks by centering the radial weight: `w = r-128`.
It computes `near*w` and `wide*w` in place with signed SIMD, then combines
`((near+wide)*128 + wide*w - near*w) >> 8`. Products fit ±32,640. This is exactly
the previous expression with one final rounding, not separate rounded products.
Three aligned PSRAM rows add 2,880 bytes (total working PSRAM 1,298,880 plus 15
alignment bytes). Opacity fading remains scalar. Pixel equivalence is tested.

Model Viewer transforms up to eight triangles (24 vertices) per batch, padding
the last group to four vertices. It computes normalized face normals, then two
batched dot operations for squared lengths and directional lighting. Degenerate
triangles retain their neutral shade. Input/render service checkpoints remain
between batches and inside rasterization. Existing two-step approximate inverse
square root is retained. Hollow Trail's fog already uses squared distance, so
there is no square root to replace there.

### Evidence and limits

Host checks cover integer alignment/tails/in-place multiplication, guarded
read-ahead, matrix layout and row counts, dot tails/alignment/overlap, owner
checks, representative batched normal lighting and Hollow Trail pixel equality.
The established model-viewer raster/shading tests also pass. Host backends model
SDK constraints; they do not execute S3 instructions. Actual device speed gains
are unmeasured, especially short four-component lighting dots and the extra
PSRAM traffic of blend scratch. Compare complete frames including packing and
API overhead, not kernel timings alone. No faster square-root claim is made.
