# Native app math acceleration (firmware 1.3.27)

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

Zero-length calls accept null pointers. Invalid size, alignment, address-range
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

The app's minimum firmware is now 1.3.27 because it imports this new getter.
Both app and firmware changes are in the same PR. Native ELF import validation
and launcher registration include the new symbol.

## Verification and limits

`test/native_apps/native_math_test.cpp` exercises alignment permutations, tails,
in-place operations, invalid bounds/overlap, task ownership, and renderer output
equivalence. Its host backend models the real assembly's extra preload under
ASan and checks all SIMD alignment/length preconditions. It does not execute S3
instructions. Firmware link/map inspection verifies the real bundled AES3
routines are present in the device build. Hardware measurements are still needed
to quantify the additional SIMD gain; host equivalence is not a device speed test.
