#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern float __divsf3(float numerator, float denominator);

static void check(uint32_t numerator, uint32_t denominator) {
    float n, d;
    memcpy(&n, &numerator, sizeof(n));
    memcpy(&d, &denominator, sizeof(d));
    volatile float vn = n, vd = d;
    const float expected = vn / vd;
    const float actual = __divsf3(vn, vd);
    uint32_t expected_bits, actual_bits;
    memcpy(&expected_bits, &expected, sizeof(expected_bits));
    memcpy(&actual_bits, &actual, sizeof(actual_bits));
    /* NaN payload and sign are implementation-defined; all other results,
     * including signed zero and rounded subnormals, must match bit for bit. */
    if ((expected_bits & UINT32_C(0x7fffffff)) > UINT32_C(0x7f800000)) {
        assert((actual_bits & UINT32_C(0x7fffffff)) > UINT32_C(0x7f800000));
    } else {
        assert(actual_bits == expected_bits);
    }
}

int main(void) {
    const uint32_t edges[] = {
        0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
        0x00800001, 0x3f000000, 0x3f7fffff, 0x3f800000, 0x3f800001,
        0x3fc00000, 0x40000000, 0x40400000, 0xbf800000, 0x7f7fffff,
        0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0x7f800001,
    };
    for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i)
        for (unsigned j = 0; j < sizeof(edges) / sizeof(edges[0]); ++j)
            check(edges[i], edges[j]);
    uint32_t state = UINT32_C(0x93a24b17);
    for (unsigned i = 0; i < 100000; ++i) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        const uint32_t numerator = state;
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        check(numerator, state);
    }
    puts("Native ELF float division boundary and randomized tests passed");
}
