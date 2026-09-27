/*
 * Native ELF compatibility implementation of the Xtensa compiler's
 * single-precision floating-point division helper. The stock libgcc archive
 * introduces ELF constructs that the firmware's deliberately restricted ELF
 * validator does not accept. Keep this helper inside only applications that
 * actually need it; do not publish it as a firmware ABI symbol.
 *
 * The implementation operates entirely on IEEE-754 binary32 fields and uses
 * restoring integer division, so compiling this file cannot recursively
 * import __divsf3 or require integer division helpers.
 */
#include <stdint.h>

typedef union {
    float value;
    uint32_t bits;
} native_float_bits_t;

static uint32_t shift_right_jam(uint32_t value, unsigned distance) {
    if (distance == 0u) return value;
    if (distance < 32u) {
        const uint32_t mask = (UINT32_C(1) << distance) - UINT32_C(1);
        return (value >> distance) | ((value & mask) != 0u);
    }
    return value != 0u;
}

static uint32_t round_significand(uint32_t extended) {
    uint32_t significand = extended >> 3;
    const uint32_t round_bits = extended & UINT32_C(7);
    if ((round_bits & UINT32_C(4)) != 0u &&
        ((round_bits & UINT32_C(3)) != 0u ||
         (significand & UINT32_C(1)) != 0u)) {
        ++significand;
    }
    return significand;
}

__attribute__((visibility("hidden")))
float __divsf3(float numerator_value, float denominator_value) {
    native_float_bits_t numerator = {.value = numerator_value};
    native_float_bits_t denominator = {.value = denominator_value};
    native_float_bits_t result;

    const uint32_t sign =
        (numerator.bits ^ denominator.bits) & UINT32_C(0x80000000);
    const uint32_t numerator_exponent =
        (numerator.bits >> 23) & UINT32_C(0xff);
    const uint32_t denominator_exponent =
        (denominator.bits >> 23) & UINT32_C(0xff);
    const uint32_t numerator_fraction =
        numerator.bits & UINT32_C(0x007fffff);
    const uint32_t denominator_fraction =
        denominator.bits & UINT32_C(0x007fffff);

    if (numerator_exponent == UINT32_C(0xff)) {
        if (numerator_fraction != 0u ||
            denominator_exponent == UINT32_C(0xff)) {
            result.bits = UINT32_C(0x7fc00000);
        } else {
            result.bits = sign | UINT32_C(0x7f800000);
        }
        return result.value;
    }
    if (denominator_exponent == UINT32_C(0xff)) {
        result.bits = denominator_fraction != 0u
                          ? UINT32_C(0x7fc00000)
                          : sign;
        return result.value;
    }
    if (denominator_exponent == 0u && denominator_fraction == 0u) {
        result.bits = (numerator_exponent == 0u && numerator_fraction == 0u)
                          ? UINT32_C(0x7fc00000)
                          : sign | UINT32_C(0x7f800000);
        return result.value;
    }
    if (numerator_exponent == 0u && numerator_fraction == 0u) {
        result.bits = sign;
        return result.value;
    }

    uint32_t numerator_significand;
    int numerator_power;
    if (numerator_exponent == 0u) {
        numerator_significand = numerator_fraction;
        numerator_power = -126;
        while (numerator_significand < UINT32_C(0x00800000)) {
            numerator_significand <<= 1;
            --numerator_power;
        }
    } else {
        numerator_significand =
            UINT32_C(0x00800000) | numerator_fraction;
        numerator_power = (int)numerator_exponent - 127;
    }

    uint32_t denominator_significand;
    int denominator_power;
    if (denominator_exponent == 0u) {
        denominator_significand = denominator_fraction;
        denominator_power = -126;
        while (denominator_significand < UINT32_C(0x00800000)) {
            denominator_significand <<= 1;
            --denominator_power;
        }
    } else {
        denominator_significand =
            UINT32_C(0x00800000) | denominator_fraction;
        denominator_power = (int)denominator_exponent - 127;
    }

    int result_power = numerator_power - denominator_power;
    if (numerator_significand < denominator_significand) {
        numerator_significand <<= 1;
        --result_power;
    }

    uint32_t quotient = 0u;
    uint32_t remainder = numerator_significand;
    for (unsigned bit = 0; bit < 27u; ++bit) {
        quotient <<= 1;
        if (remainder >= denominator_significand) {
            remainder -= denominator_significand;
            quotient |= UINT32_C(1);
        }
        remainder <<= 1;
    }
    if (remainder != 0u) quotient |= UINT32_C(1);

    if (result_power < -126) {
        quotient = shift_right_jam(quotient, (unsigned)(-126 - result_power));
        const uint32_t significand = round_significand(quotient);
        result.bits = significand >= UINT32_C(0x00800000)
                          ? sign | UINT32_C(0x00800000)
                          : sign | significand;
        return result.value;
    }

    uint32_t significand = round_significand(quotient);
    if (significand == UINT32_C(0x01000000)) {
        significand >>= 1;
        ++result_power;
    }
    if (result_power > 127) {
        result.bits = sign | UINT32_C(0x7f800000);
        return result.value;
    }

    result.bits = sign |
                  ((uint32_t)(result_power + 127) << 23) |
                  (significand & UINT32_C(0x007fffff));
    return result.value;
}
