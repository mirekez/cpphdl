#include "IntegerMath.h"
#include "Transformer.h"
#include <cstdio>
#include <random>
#include <stdexcept>
#include "fp_math.h"
#include "qmath.hpp"

static_assert(integer_llm::Layout<integer_llm::QwenModel>::rope == 494032768,
              "the full model's tensor shapes must match the original weight count");

int main() {
    try {
        std::mt19937_64 random(0x51483438);
        for (unsigned i = 0; i < 10000; ++i) {
            int64_t a = int64_t(random() % (uint64_t(1) << 55)) - (int64_t(1) << 54);
            int64_t b = int64_t(random() % (uint64_t(1) << 51)) + 1;
            if (integer_llm::multiply(a, b) != fp_mul(a, b) ||
                integer_llm::divide(a, b) != fp_div(a, b) ||
                integer_llm::inverse_sqrt(b) != fp_inv_sqrt(b) ||
                integer_llm::exp_negative(-b) != qmath::exp_negative(-b) ||
                integer_llm::silu(a) != qmath::silu(a))
                throw std::runtime_error("original intllm arithmetic mismatch");
        }
        for (int64_t x : {int64_t(0), int64_t(1), int64_t(2), integer_llm::one, INT64_MAX})
            if (integer_llm::inverse_sqrt(x) != fp_inv_sqrt(x)) throw std::runtime_error("inverse sqrt edge case mismatch");
        std::puts("50,000 Q16.48 arithmetic comparisons against original intllm passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
