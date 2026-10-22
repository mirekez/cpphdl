#include "../math/Nonlinear.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace cpphdl::hls::math;

static float fromBits(uint32_t value) {
    float result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
static uint32_t toBits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
static uint32_t flush(uint32_t value) {
    if (!(value & 0x7f800000u)) return value & 0x80000000u;
    if ((value & 0x7fffffffu) > 0x7f800000u) return 0x7fc00000u;
    return value;
}
static void check(const char* name, uint32_t a, uint32_t b, uint32_t got, uint32_t expected) {
    if (got != expected) {
        std::fprintf(stderr, "%s a=%08x b=%08x got=%08x expected=%08x\n", name, a, b, got, expected);
        throw std::runtime_error("FP32 math mismatch");
    }
}
int main() try {
    uint32_t random = 0x31415926u;
    auto next = [&] {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        return random;
    };
    const uint32_t special[] = {0, 0x80000000u, 1, 0x007fffffu, 0x00800000u,
        0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0x7f800000u, 0xff800000u, 0x7fc12345u};
    for (unsigned i = 0; i < 200000; ++i) {
        uint32_t a = i < 121 ? special[i / 11] : next();
        uint32_t b = i < 121 ? special[i % 11] : next();
        float x = fromBits(flush(a)), y = fromBits(flush(b));
        check("add", a, b, fp32_add(a, b), flush(toBits(x + y)));
        check("multiply", a, b, fp32_multiply(a, b), flush(toBits(x * y)));
        if (i < 10000) {
            check("divide", a, b, fp32_divide(a, b), flush(toBits(x / y)));
            check("sqrt", a, 0, fp32_sqrt(a), flush(toBits(std::sqrt(x))));
        }
        check("int32", a, 0, fp32_from_int(int32_t(a)), toBits(float(int32_t(a))));
        check("uint32", a, 0, unsigned_to_float_bits(a), toBits(float(a)));
        check("transport", a, 0, encode(decode(a)), a);
    }
    for (unsigned raw = 0; raw < 65536; ++raw) {
        unsigned exponent = (raw >> 10) & 31, mantissa = raw & 1023;
        uint32_t expected;
        if (exponent == 31) expected = ((raw & 0x8000u) << 16) | 0x7f800000u | (mantissa << 13);
        else {
            float value = exponent ? std::ldexp(float(1024 + mantissa), int(exponent) - 25)
                                   : std::ldexp(float(mantissa), -24);
            expected = toBits((raw & 0x8000u) ? -value : value);
        }
        check("fp16", raw, 0, fp16_to_fp32(raw), expected);
    }
    for (int sample = -3300; sample <= 3300; ++sample) {
        float x = float(sample) / 37.0f;
        float expected = fromBits(flush(toBits(std::exp(x))));
        float got = fromBits(fp32_exp(toBits(x)));
        if (std::isinf(expected)) {
            if (!std::isinf(got)) throw std::runtime_error("exp overflow mismatch");
        } else if (std::fabs(got - expected) > 2e-5f * expected)
            throw std::runtime_error("exp approximation error");
    }
    std::puts("FP32 RNE/FTZ math: 200000 arithmetic samples, 65536 FP16 values, nonlinear references passed");
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
