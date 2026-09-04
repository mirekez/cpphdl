#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

// One-stage floating-point adder. Alignment is combinational before sum_clock,
// the signed significand addition is registered, and normalization/rounding is
// combinational after the register. Exponent-zero values are flushed to zero.
template<size_t W, size_t EW>
class FpAdd : public Module
{
    static constexpr size_t MANT_WIDTH = W - EW - 1;
    static constexpr size_t SUM_WIDTH = ((MANT_WIDTH + 6 + 7) / 8) * 8;
    static constexpr uint64_t MANT_MASK = (uint64_t(1) << MANT_WIDTH) - 1;
    static constexpr uint64_t EXP_MAX = (uint64_t(1) << EW) - 1;
    static constexpr uint64_t SIGN_MASK = uint64_t(1) << (W - 1);
    static constexpr uint64_t SUM_MASK = (uint64_t(1) << SUM_WIDTH) - 1;

    static_assert(W >= 4 && W <= 32, "FpAdd supports widths from 4 to 32 bits");
    static_assert(EW >= 2 && EW < W - 1, "FpAdd exponent width is invalid");

public:
    _PORT(logic<W>) a_in;
    _PORT(logic<W>) b_in;
    _PORT(bool) valid_in;
    _PORT(logic<W>) data_out = _ASSIGN_COMB(result_comb_func());
    _PORT(bool) valid_out = _ASSIGN_REG(valid_reg);

private:
    reg<logic<SUM_WIDTH>> sum_reg;
    reg<u<EW>> exponent_reg;
    reg<logic<W>> special_result_reg;
    reg<u1> special_reg;
    reg<u1> valid_reg;

    logic<SUM_WIDTH> add_lhs_comb;
    logic<SUM_WIDTH> add_rhs_comb;
    u<EW> exponent_comb;
    logic<W> special_result_comb;
    bool special_comb;

    logic<SUM_WIDTH>& add_lhs_comb_func()
    {
        uint64_t raw_a;
        uint64_t raw_b;
        uint64_t exponent_a;
        uint64_t exponent_b;
        uint64_t mantissa_a;
        uint64_t significand_a;
        uint64_t aligned_a;
        uint64_t discarded;
        uint64_t shift_mask;
        logic<64> encoded;
        uint32_t shift;
        bool sign_a;
        bool special;

        raw_a = (uint64_t)a_in();
        raw_b = (uint64_t)b_in();
        exponent_a = (raw_a >> MANT_WIDTH) & EXP_MAX;
        exponent_b = (raw_b >> MANT_WIDTH) & EXP_MAX;
        mantissa_a = raw_a & MANT_MASK;
        sign_a = (raw_a & SIGN_MASK) != 0;
        special = exponent_a == 0 || exponent_b == 0
            || exponent_a == EXP_MAX || exponent_b == EXP_MAX;
        significand_a = 0;
        aligned_a = 0;
        discarded = 0;
        shift_mask = 0;
        encoded = 0;
        shift = 0;
        add_lhs_comb = 0;

        if (!special) {
            significand_a = ((uint64_t(1) << MANT_WIDTH) | mantissa_a) << 3;
            aligned_a = significand_a;
            if (exponent_a < exponent_b) {
                shift = (uint32_t)(exponent_b - exponent_a);
                if (shift >= SUM_WIDTH) {
                    aligned_a = significand_a != 0 ? 1 : 0;
                }
                else if (shift != 0) {
                    shift_mask = (uint64_t(1) << shift) - 1;
                    discarded = significand_a & shift_mask;
                    aligned_a = significand_a >> shift;
                    if (discarded != 0) {
                        aligned_a |= 1;
                    }
                }
            }
            encoded = sign_a
                ? ((uint64_t(0) - aligned_a) & SUM_MASK) : aligned_a;
            add_lhs_comb = encoded.bits(SUM_WIDTH - 1, 0);
        }
        return add_lhs_comb;
    }

    logic<SUM_WIDTH>& add_rhs_comb_func()
    {
        uint64_t raw_a;
        uint64_t raw_b;
        uint64_t exponent_a;
        uint64_t exponent_b;
        uint64_t mantissa_b;
        uint64_t significand_b;
        uint64_t aligned_b;
        uint64_t discarded;
        uint64_t shift_mask;
        logic<64> encoded;
        uint32_t shift;
        bool sign_b;
        bool special;

        raw_a = (uint64_t)a_in();
        raw_b = (uint64_t)b_in();
        exponent_a = (raw_a >> MANT_WIDTH) & EXP_MAX;
        exponent_b = (raw_b >> MANT_WIDTH) & EXP_MAX;
        mantissa_b = raw_b & MANT_MASK;
        sign_b = (raw_b & SIGN_MASK) != 0;
        special = exponent_a == 0 || exponent_b == 0
            || exponent_a == EXP_MAX || exponent_b == EXP_MAX;
        significand_b = 0;
        aligned_b = 0;
        discarded = 0;
        shift_mask = 0;
        encoded = 0;
        shift = 0;
        add_rhs_comb = 0;

        if (!special) {
            significand_b = ((uint64_t(1) << MANT_WIDTH) | mantissa_b) << 3;
            aligned_b = significand_b;
            if (exponent_b < exponent_a) {
                shift = (uint32_t)(exponent_a - exponent_b);
                if (shift >= SUM_WIDTH) {
                    aligned_b = significand_b != 0 ? 1 : 0;
                }
                else if (shift != 0) {
                    shift_mask = (uint64_t(1) << shift) - 1;
                    discarded = significand_b & shift_mask;
                    aligned_b = significand_b >> shift;
                    if (discarded != 0) {
                        aligned_b |= 1;
                    }
                }
            }
            encoded = sign_b
                ? ((uint64_t(0) - aligned_b) & SUM_MASK) : aligned_b;
            add_rhs_comb = encoded.bits(SUM_WIDTH - 1, 0);
        }
        return add_rhs_comb;
    }

    u<EW>& exponent_comb_func()
    {
        uint64_t exponent_a;
        uint64_t exponent_b;
        logic<64> exponent_wide;

        exponent_a = ((uint64_t)a_in() >> MANT_WIDTH) & EXP_MAX;
        exponent_b = ((uint64_t)b_in() >> MANT_WIDTH) & EXP_MAX;
        exponent_wide = 0;
        exponent_comb = 0;
        if (exponent_a != 0 && exponent_b != 0
            && exponent_a != EXP_MAX && exponent_b != EXP_MAX) {
            exponent_wide = exponent_a >= exponent_b ? exponent_a : exponent_b;
            exponent_comb = exponent_wide.bits(EW - 1, 0);
        }
        return exponent_comb;
    }

    bool& special_comb_func()
    {
        uint64_t exponent_a;
        uint64_t exponent_b;

        exponent_a = ((uint64_t)a_in() >> MANT_WIDTH) & EXP_MAX;
        exponent_b = ((uint64_t)b_in() >> MANT_WIDTH) & EXP_MAX;
        special_comb = exponent_a == 0 || exponent_b == 0
            || exponent_a == EXP_MAX || exponent_b == EXP_MAX;
        return special_comb;
    }

    logic<W>& special_result_comb_func()
    {
        uint64_t raw_a;
        uint64_t raw_b;
        uint64_t exponent_a;
        uint64_t exponent_b;
        uint64_t mantissa_a;
        uint64_t mantissa_b;
        logic<64> result_wide;
        bool sign_a;
        bool sign_b;
        bool zero_a;
        bool zero_b;
        bool infinity_a;
        bool infinity_b;
        bool nan_a;
        bool nan_b;

        raw_a = (uint64_t)a_in();
        raw_b = (uint64_t)b_in();
        exponent_a = (raw_a >> MANT_WIDTH) & EXP_MAX;
        exponent_b = (raw_b >> MANT_WIDTH) & EXP_MAX;
        mantissa_a = raw_a & MANT_MASK;
        mantissa_b = raw_b & MANT_MASK;
        sign_a = (raw_a & SIGN_MASK) != 0;
        sign_b = (raw_b & SIGN_MASK) != 0;
        zero_a = exponent_a == 0;
        zero_b = exponent_b == 0;
        infinity_a = exponent_a == EXP_MAX && mantissa_a == 0;
        infinity_b = exponent_b == EXP_MAX && mantissa_b == 0;
        nan_a = exponent_a == EXP_MAX && mantissa_a != 0;
        nan_b = exponent_b == EXP_MAX && mantissa_b != 0;
        result_wide = 0;
        special_result_comb = 0;

        if (nan_a || nan_b || (infinity_a && infinity_b && sign_a != sign_b)) {
            result_wide = (EXP_MAX << MANT_WIDTH) | 1;
        }
        else if (infinity_a) {
            result_wide = (sign_a ? SIGN_MASK : 0) | (EXP_MAX << MANT_WIDTH);
        }
        else if (infinity_b) {
            result_wide = (sign_b ? SIGN_MASK : 0) | (EXP_MAX << MANT_WIDTH);
        }
        else if (zero_a && zero_b) {
            result_wide = sign_a && sign_b ? SIGN_MASK : 0;
        }
        else if (zero_a) {
            result_wide = raw_b;
        }
        else if (zero_b) {
            result_wide = raw_a;
        }
        special_result_comb = result_wide.bits(W - 1, 0);
        return special_result_comb;
    }

    logic<W> result_comb;
    logic<W>& result_comb_func()
    {
        uint64_t sum;
        uint64_t magnitude;
        uint64_t retained;
        logic<64> result_raw;
        uint64_t discarded;
        int exponent;
        bool negative;
        bool guard;
        bool round;
        bool sticky;
        uint8_t i;

        sum = (uint64_t)sum_reg;
        magnitude = 0;
        retained = 0;
        result_raw = 0;
        discarded = 0;
        exponent = (int)(uint64_t)exponent_reg;
        negative = (sum & (uint64_t(1) << (SUM_WIDTH - 1))) != 0;
        guard = false;
        round = false;
        sticky = false;
        result_comb = 0;

        if (!valid_reg) {
            result_raw = 0;
        }
        else if (special_reg) {
            result_raw = (uint64_t)special_result_reg;
        }
        else {
            magnitude = negative ? ((uint64_t(0) - sum) & SUM_MASK) : sum;
            if (magnitude == 0) {
                result_raw = 0;
            }
            else {
                if ((magnitude & (uint64_t(1) << (MANT_WIDTH + 4))) != 0) {
                    discarded = magnitude & 1;
                    magnitude >>= 1;
                    if (discarded != 0) {
                        magnitude |= 1;
                    }
                    ++exponent;
                }
                for (i = 0; i < 32; ++i) {
                    if ((magnitude & (uint64_t(1) << (MANT_WIDTH + 3))) == 0
                        && magnitude != 0 && exponent > 0) {
                        magnitude <<= 1;
                        --exponent;
                    }
                }
                if (exponent <= 0) {
                    result_raw = negative ? SIGN_MASK : 0;
                }
                else if (exponent >= (int)EXP_MAX) {
                    result_raw = (negative ? SIGN_MASK : 0) | (EXP_MAX << MANT_WIDTH);
                }
                else {
                    retained = magnitude >> 3;
                    guard = ((magnitude >> 2) & 1) != 0;
                    round = ((magnitude >> 1) & 1) != 0;
                    sticky = (magnitude & 1) != 0;
                    if (guard && (round || sticky || (retained & 1) != 0)) {
                        ++retained;
                    }
                    if (retained >= (uint64_t(1) << (MANT_WIDTH + 1))) {
                        retained >>= 1;
                        ++exponent;
                    }
                    if (exponent >= (int)EXP_MAX) {
                        result_raw = (negative ? SIGN_MASK : 0) | (EXP_MAX << MANT_WIDTH);
                    }
                    else {
                        result_raw = (negative ? SIGN_MASK : 0)
                            | ((uint64_t)exponent << MANT_WIDTH)
                            | (retained & MANT_MASK);
                    }
                }
            }
        }

        result_comb = result_raw.bits(W - 1, 0);
        return result_comb;
    }

public:
    void _work(bool reset)
    {
        sum_reg._next = add_lhs_comb_func() + add_rhs_comb_func();
        exponent_reg._next = exponent_comb_func();
        special_result_reg._next = special_result_comb_func();
        special_reg._next = special_comb_func();
        valid_reg._next = valid_in();

        if (reset) {
            sum_reg.clr();
            exponent_reg.clr();
            special_result_reg.clr();
            special_reg.clr();
            valid_reg.clr();
        }
    }

    void _strobe()
    {
        sum_reg.strobe();
        exponent_reg.strobe();
        special_result_reg.strobe();
        special_reg.strobe();
        valid_reg.strobe();
    }

    void _assign() {}
};

template class FpAdd<16, 5>;
template class FpAdd<32, 8>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include "FpMathTest.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "../tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static bool generated_sv_has_registered_sum()
{
    std::ifstream input("generated/FpAdd.sv");
    std::string text;

    if (!input) {
        std::printf("can't open generated/FpAdd.sv\n");
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return text.find("input wire sum_clock") != std::string::npos
        && text.find("always @(posedge sum_clock)") != std::string::npos
        && text.find("always_comb begin : add_lhs_comb_func") != std::string::npos
        && text.find("always_comb begin : add_rhs_comb_func") != std::string::npos
        && text.find("prepare_comb_func") == std::string::npos
        && text.find("add_lhs_comb + add_rhs_comb") != std::string::npos;
}

template<size_t W, size_t EW>
class TestFpAdd
{
    using Format = FpMathTestFormat<W, EW>;

#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    FpAdd<W, EW> dut;
    logic<W> a_data = 0;
    logic<W> b_data = 0;
    bool input_valid = false;
#endif
    bool debug;
    bool error = false;
    bool expected_valid = false;
    uint64_t expected_data = 0;

    static double operand_value(uint64_t raw)
    {
        if (Format::exponent(raw) == 0) {
            return Format::sign(raw) ? -0.0 : 0.0;
        }
        return Format::to_double(raw);
    }

    static uint64_t reference(uint64_t a, uint64_t b)
    {
        return Format::from_double(operand_value(a) + operand_value(b));
    }

    void set_inputs(uint64_t a, uint64_t b, bool valid, bool reset)
    {
#ifdef VERILATOR
        dut.a_in = a;
        dut.b_in = b;
        dut.valid_in = valid;
        dut.reset = reset;
#else
        a_data = (logic<W>)a;
        b_data = (logic<W>)b;
        input_valid = valid;
#endif
    }

    bool output_valid()
    {
#ifdef VERILATOR
        return dut.valid_out;
#else
        return dut.valid_out();
#endif
    }

    uint64_t output_data()
    {
#ifdef VERILATOR
        return (uint64_t)dut.data_out & Format::RAW_MASK;
#else
        return (uint64_t)dut.data_out() & Format::RAW_MASK;
#endif
    }

    void check_output(const char* phase, uint64_t a, uint64_t b)
    {
        uint64_t actual;

        actual = output_data();
        if (output_valid() != expected_valid
            || (expected_valid && actual != expected_data)) {
            std::printf("FpAdd<%zu,%zu> ERROR %s a=%08llx b=%08llx valid=%u/%u output=%08llx expected=%08llx\n",
                W, EW, phase, (unsigned long long)a, (unsigned long long)b,
                (unsigned)output_valid(), (unsigned)expected_valid,
                (unsigned long long)actual, (unsigned long long)expected_data);
            error = true;
        }
        else if (debug && expected_valid) {
            std::printf("FpAdd<%zu,%zu> a=%08llx b=%08llx output=%08llx\n",
                W, EW, (unsigned long long)a, (unsigned long long)b,
                (unsigned long long)actual);
        }
    }

    void cycle(uint64_t a, uint64_t b, bool valid, bool reset = false)
    {
        set_inputs(a, b, valid, reset);
#ifdef VERILATOR
        dut.sum_clock = 0;
        dut.eval();
#endif
        check_output("before edge", a, b);

#ifdef VERILATOR
        dut.sum_clock = 1;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
#endif
        ++_system_clock;
        expected_valid = !reset && valid;
        expected_data = expected_valid ? reference(a, b) : 0;
        check_output("after edge", a, b);

#ifdef VERILATOR
        dut.sum_clock = 0;
        dut.eval();
#endif
    }

    void directed_tests()
    {
        cycle(Format::zero(false), Format::zero(false), true);
        cycle(Format::zero(true), Format::zero(true), true);
        cycle(Format::one(false), Format::one(false), true);
        cycle(Format::one(false), Format::one(true), true);
        cycle(Format::infinity(false), Format::one(false), true);
        cycle(Format::infinity(true), Format::one(false), true);
        cycle(Format::infinity(false), Format::infinity(true), true);
        cycle(Format::nan(), Format::one(false), true);
        cycle(Format::from_double(1.5), Format::from_double(2.25), true);
        cycle(Format::from_double(65504.0), Format::from_double(65504.0), true);
        cycle(Format::from_double(1.0), Format::from_double(0.0009765625), true);
        cycle(0, 0, false);
    }

public:
    explicit TestFpAdd(bool debug_in) : debug(debug_in)
    {
#ifndef VERILATOR
        dut.a_in = _ASSIGN(a_data);
        dut.b_in = _ASSIGN(b_data);
        dut.valid_in = _ASSIGN(input_valid);
        dut._assign();
#else
        dut.sum_clock = 0;
#endif
    }

    bool run()
    {
        uint64_t a;
        uint64_t b;
        uint64_t state;
        size_t i;
        size_t iterations;
        auto start = std::chrono::high_resolution_clock::now();

#ifdef VERILATOR
        std::printf("VERILATOR FpAdd<%zu,%zu>...", W, EW);
#else
        std::printf("CppHDL FpAdd<%zu,%zu>...", W, EW);
#endif
        cycle(0, 0, false, true);
        cycle(0, 0, false, true);
        directed_tests();

        state = 0x2d99787926d46932ULL;
        iterations = W <= 16 ? 65536 : 100000;
        for (i = 0; i < iterations && !error; ++i) {
            a = W <= 16 ? i : fp_math_random_step(state);
            b = fp_math_random_step(state);
            cycle(a & Format::RAW_MASK, b & Format::RAW_MASK, true);
        }
        cycle(0, 0, false);

        std::printf(" %s (%lld microseconds)\n", error ? "FAILED" : "PASSED",
            (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    bool ok = true;
    std::vector<std::string> positional;
    size_t i;

    for (i = 1; i < (size_t)argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        else if (std::strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        else {
            positional.emplace_back(argv[i]);
        }
    }

#ifndef VERILATOR
    ok = generated_sv_has_registered_sum();
    if (!noveril && ok) {
        std::cout << "Building FpAdd Verilator simulation...\n";
        ok &= VerilatorCompile(__FILE__, "FpAdd", {"Predef_pkg"}, {"../../../../include"}, 16, 5);
        ok &= VerilatorCompile(__FILE__, "FpAdd", {"Predef_pkg"}, {"../../../../include"}, 32, 8);
        if (ok) {
            ok &= std::system((std::string("FpAdd_16_5/obj_dir/VFpAdd 16 5")
                + (debug ? " --debug" : "")).c_str()) == 0;
            ok &= std::system((std::string("FpAdd_32_8/obj_dir/VFpAdd 32 8")
                + (debug ? " --debug" : "")).c_str()) == 0;
        }
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    if (positional.size() >= 2) {
        size_t width = std::stoull(positional[0]);
        size_t exponent_width = std::stoull(positional[1]);
        if (width == 16 && exponent_width == 5) {
            return !(ok && TestFpAdd<16, 5>(debug).run());
        }
        if (width == 32 && exponent_width == 8) {
            return !(ok && TestFpAdd<32, 8>(debug).run());
        }
        std::printf("Unsupported FpAdd format: W=%zu EW=%zu\n", width, exponent_width);
        return 1;
    }

    ok = ok && TestFpAdd<16, 5>(debug).run();
    ok = ok && TestFpAdd<32, 8>(debug).run();
    return !ok;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
