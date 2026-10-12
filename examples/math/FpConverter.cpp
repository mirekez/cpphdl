#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#define CPPHDL_DISABLE_RAW_FIELD_FORMATTER
#include <cpphdl.h>
#include <print>

using namespace cpphdl;

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "../../hls/math/FpConverter.h"

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include <vector>
#include "../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

template<typename STYPE, typename DTYPE>
static bool check_ref(const char* name, STYPE in, uint64_t expected_raw)
{
    DTYPE out;
    in.convert(out);
    if ((uint64_t)out.raw != expected_raw) {
        std::print("REFERENCE ERROR {}: input {} produced {}, expected {:x}\n",
            name, in, out, expected_raw);
        return false;
    }
    return true;
}

static uint64_t random_step(uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

template<typename STYPE>
static STYPE make_random_normal(uint64_t& state)
{
    bool sign;
    uint64_t exp;
    uint64_t mant;

    sign = (random_step(state) & 1) != 0;
    exp = 1 + (random_step(state) % (STYPE::EXP_MAX - 1));
    mant = random_step(state) & STYPE::MANT_MAX;
    return STYPE::make(sign, exp, mant);
}

template<typename STYPE, typename DTYPE>
static bool run_random_ref(const char* name, size_t count)
{
    size_t i;
    uint64_t state = 0x123456789abcdef0ULL ^ (STYPE::WIDTH << 24) ^ (STYPE::EXP_WIDTH << 16) ^ (DTYPE::WIDTH << 8) ^ DTYPE::EXP_WIDTH;
    bool ok = true;

    for (i = 0; i < count; ++i) {
        STYPE in = make_random_normal<STYPE>(state);
        DTYPE out;
        DTYPE ref;

        in.convert(out);
        ref.from_double(in.to_double());
        if ((uint64_t)out.raw != (uint64_t)ref.raw) {
            std::print("RANDOM REFERENCE ERROR {}[{}]: input {} ({}) produced {}, expected {}\n",
                name, i, in, in.to_double(), out, ref);
            ok = false;
            break;
        }
    }
    return ok;
}

static bool run_reference_tests()
{
    bool ok = true;
    ok &= check_ref<FP32E8,FP16E5>("fp32 +0 -> fp16 +0", FP32E8::from_raw(0x00000000), 0x0000);
    ok &= check_ref<FP32E8,FP16E5>("fp32 -0 -> fp16 -0", FP32E8::from_raw(0x80000000), 0x8000);
    ok &= check_ref<FP32E8,FP16E5>("fp32 +inf -> fp16 +inf", FP32E8::from_raw(0x7f800000), 0x7c00);
    ok &= check_ref<FP32E8,FP16E5>("fp32 -inf -> fp16 -inf", FP32E8::from_raw(0xff800000), 0xfc00);
    ok &= check_ref<FP16E5,FP32E8>("fp16 1.0 -> fp32 1.0", FP16E5::from_raw(0x3c00), 0x3f800000);
    ok &= check_ref<BF16E8,FP16E5>("bf16 1.0 -> fp16 1.0", BF16E8::from_raw(0x3f80), 0x3c00);
    ok &= check_ref<FP32E8,BF16E8>("rne below half", FP32E8::from_raw(0x3f807fff), 0x3f80);
    ok &= check_ref<FP32E8,BF16E8>("rne tie even", FP32E8::from_raw(0x3f808000), 0x3f80);
    ok &= check_ref<FP32E8,BF16E8>("rne tie odd", FP32E8::from_raw(0x3f818000), 0x3f82);
    ok &= check_ref<FP32E8,BF16E8>("rne above half", FP32E8::from_raw(0x3f808001), 0x3f81);
    ok &= check_ref<FP32E8,FP16E5>("fp32 max finite -> fp16 +inf", FP32E8::max_finite(false), 0x7c00);
    ok &= check_ref<FP32E8,FP16E5>("fp32 min normal -> fp16 +0", FP32E8::min_normal(false), 0x0000);
    ok &= check_ref<FP8E4,FP4E2>("fp8 1.0 -> fp4 1.0", FP8E4::one(false), 0x2);
    ok &= check_ref<FP4E2,FP16E5>("fp4 1.0 -> fp16 1.0", FP4E2::one(false), 0x3c00);

    ok &= run_random_ref<FP32E8,FP16E5>("fp32 -> fp16 random double reference", 1000);
    ok &= run_random_ref<FP16E5,FP32E8>("fp16 -> fp32 random double reference", 1000);
    ok &= run_random_ref<FP32E8,BF16E8>("fp32 -> bf16 random double reference", 1000);
    ok &= run_random_ref<BF16E8,FP32E8>("bf16 -> fp32 random double reference", 1000);
    ok &= run_random_ref<FP16E5,BF16E8>("fp16 -> bf16 random double reference", 1000);
    ok &= run_random_ref<BF16E8,FP16E5>("bf16 -> fp16 random double reference", 1000);
    ok &= run_random_ref<FP32E8,TF19E8>("fp32 -> tf19 random double reference", 1000);
    ok &= run_random_ref<TF19E8,FP16E5>("tf19 -> fp16 random double reference", 1000);
    ok &= run_random_ref<FP16E5,FP8E4>("fp16 -> fp8 random double reference", 1000);
    ok &= run_random_ref<FP8E4,FP4E2>("fp8 -> fp4 random double reference", 1000);
    ok &= run_random_ref<FP4E2,FP16E5>("fp4 -> fp16 random double reference", 1000);
    return ok;
}

template<typename STYPE, typename DTYPE, size_t LENGTH, bool USE_REG>
class TestFpConverter : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL converter;
#else
    FpConverter<STYPE,DTYPE,LENGTH,USE_REG> converter;
#endif

    reg<array<LENGTH,STYPE>> out_reg;
    reg<array<LENGTH,DTYPE>> expected1;
    reg<array<LENGTH,DTYPE>> expected2;
    reg<u1> can_check1;
    reg<u1> can_check2;
    bool error;

    array<LENGTH,DTYPE>    read_data;  // to support Verilator

public:

    bool debugen_in;

    TestFpConverter(bool debug)
    {
        debugen_in = debug;
    }

    ~TestFpConverter()
    {
    }

    static STYPE make_input(size_t index, uint64_t cycle)
    {
        STYPE out;
        uint64_t exp;
        uint64_t mant;
        switch ((index + cycle) % 10) {
            case 0: return STYPE::zero(false);
            case 1: return STYPE::zero(true);
            case 2: return STYPE::one(false);
            case 3: return STYPE::one(true);
            case 4: return STYPE::inf(false);
            case 5: return STYPE::inf(true);
            case 6: return STYPE::nan();
            case 7: return STYPE::min_normal(false);
            case 8: return STYPE::max_finite(false);
            default:
                exp = 1 + ((cycle * 17 + index * 5) % (STYPE::EXP_MAX - 1));
                mant = (cycle * 0x9e3779b97f4a7c15ULL + index * 0x12345ULL) & STYPE::MANT_MAX;
                out = STYPE::make(((cycle + index) & 1) != 0, exp, mant);
                return out;
        }
    }

    static DTYPE reference_convert(const STYPE& in)
    {
        DTYPE out;
        const_cast<STYPE&>(in).convert(out);
        return out;
    }

#ifdef VERILATOR
    template<typename TYPE>
    static constexpr size_t verilator_type_bits()
    {
        return sizeof(typename TYPE::RAW) * 8;
    }

    static void set_port_bits(uint32_t* port, size_t first, size_t width, uint64_t value)
    {
        size_t bit;
        for (bit = 0; bit < width; ++bit) {
            const size_t pos = first + bit;
            const uint32_t mask = uint32_t(1) << (pos & 31);
            if ((value >> bit) & 1ULL) {
                port[pos >> 5] |= mask;
            }
            else {
                port[pos >> 5] &= ~mask;
            }
        }
    }

    static uint64_t get_port_bits(const uint32_t* port, size_t first, size_t width)
    {
        size_t bit;
        uint64_t value = 0;
        for (bit = 0; bit < width; ++bit) {
            const size_t pos = first + bit;
            if ((port[pos >> 5] >> (pos & 31)) & 1U) {
                value |= 1ULL << bit;
            }
        }
        return value;
    }

    void pack_input_port()
    {
        size_t i;
        constexpr size_t bits = verilator_type_bits<STYPE>();
        std::memset(converter.data_in.data(), 0, sizeof(converter.data_in));
        for (i = 0; i < LENGTH; ++i) {
            set_port_bits(converter.data_in.data(), i * bits, bits, (uint64_t)out_reg[i].raw);
        }
    }

    void unpack_output_port()
    {
        size_t i;
        constexpr size_t bits = verilator_type_bits<DTYPE>();
        for (i = 0; i < LENGTH; ++i) {
            read_data[i].raw = (typename DTYPE::RAW)get_port_bits(converter.data_out.data(), i * bits, bits);
        }
    }
#endif

    void _assign()
    {
#ifndef VERILATOR
        converter.__inst_name = __inst_name + "/converter";

        converter.data_in      = _ASSIGN_REG( out_reg );
        converter.debugen_in   = debugen_in;
        converter._assign();
#endif
    }

    void _work(bool reset)
    {
        size_t i;
        if (reset) {
            error = false;
            out_reg.clr();
            can_check1.clr();
            can_check2.clr();
#ifdef VERILATOR
            pack_input_port();
            converter.debugen_in = debugen_in;
            converter.clk = 1;
            converter.reset = 1;
            converter.eval();
#endif
            return;
        }

#ifdef VERILATOR
        // we're using this trick to update comb values of Verilator on it's outputs without strobing registers
        // the problem is that it's difficult to see 0-delayed memory output from Verilator
        // because if we write the same cycle Verilator updates combs in eval() and we see same clock written words
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
        pack_input_port();
        converter.clk = 0;
        converter.reset = 0;
        converter.eval();  // so lets update Verilator's combs without strobing registers
        unpack_output_port();
#else
        read_data = converter.data_out();
#endif
        // test result
        for (i=0; i < LENGTH; ++i) {
            DTYPE expected = USE_REG ? expected2[i] : expected1[i];
            if (!reset && ((!USE_REG && can_check1 && read_data[i].raw != expected.raw)
                         || (USE_REG && can_check2 && read_data[i].raw != expected.raw))) {
                std::print("{:s} ERROR: {} was read instead of {} at lane {}\n",
                    __inst_name,
                    read_data[i],
                    expected,
                    i);
                error = true;
            }
        }

        for (i=0; i < LENGTH; ++i) {
            STYPE input = make_input(i, (uint64_t)_system_clock);
            out_reg._next[i] = input;
            expected1._next[i] = reference_convert(input);
        }
        expected2._next = expected1;
        can_check1._next = 1;
        can_check2._next = can_check1;

#ifndef VERILATOR
        converter._work(reset);
#else
        pack_input_port();
        converter.debugen_in = debugen_in;

        converter.clk = 1;
        converter.reset = reset;
        converter.eval();  // eval of verilator should be in the end in 0-delay test
#endif
    }

    void _strobe()
    {
#ifndef VERILATOR
        converter._strobe();
#endif
        out_reg.strobe();
        expected1.strobe();
        expected2.strobe();
        can_check1.strobe();
        can_check2.strobe();
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        converter.clk = 0;
        converter.reset = reset;
        converter.eval();  // eval of verilator should be in the end
#endif
    }

    void _strobe_neg()
    {
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestFpConverter, MODEL: {}, LENGTH: {}, USE_REG: {}...", STRINGIFY(VERILATOR_MODEL), LENGTH, USE_REG);
#else
        std::print("C++HDL TestFpConverter, STYPE: {}, DTYPE: {}, LENGTH: {}, USE_REG: {}...", typeid(STYPE).name(), typeid(DTYPE).name(), LENGTH, USE_REG);
#endif
        if (debugen_in) {
            std::print("\n");
        }

        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "converter_test";
        _assign();
        _work(1);
        _work_neg(1);

        int cycles = 100000;
        while (--cycles) {
            _strobe();
            ++_system_clock;
            _work(0);
            _strobe_neg();
            _work_neg(0);

            if (error) {
                break;
            }
        }
        std::print(" {} ({} microseconds)\n", !error?"PASSED":"FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main (int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    std::vector<std::string> positional;
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        else if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        else {
            positional.emplace_back(argv[i]);
        }
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "FpConverterFP32_8_FP16_5", {"Predef_pkg","FP16_5_pkg","FP32_8_pkg"}, {"../../../../include"}, 16, 1);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP16_5_FP32_8", {"Predef_pkg","FP16_5_pkg","FP32_8_pkg"}, {"../../../../include"}, 16, 0);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP32_8_FP16_8", {"Predef_pkg","FP16_8_pkg","FP32_8_pkg"}, {"../../../../include"}, 16, 1);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP16_8_FP32_8", {"Predef_pkg","FP16_8_pkg","FP32_8_pkg"}, {"../../../../include"}, 16, 0);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP16_5_FP16_8", {"Predef_pkg","FP16_5_pkg","FP16_8_pkg"}, {"../../../../include"}, 16, 1);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP16_8_FP16_5", {"Predef_pkg","FP16_5_pkg","FP16_8_pkg"}, {"../../../../include"}, 16, 0);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP32_8_FP19_8", {"Predef_pkg","FP19_8_pkg","FP32_8_pkg"}, {"../../../../include"}, 16, 1);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP19_8_FP16_5", {"Predef_pkg","FP16_5_pkg","FP19_8_pkg"}, {"../../../../include"}, 16, 0);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP16_5_FP8_4", {"Predef_pkg","FP8_4_pkg","FP16_5_pkg"}, {"../../../../include"}, 16, 1);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP8_4_FP4_2", {"Predef_pkg","FP8_4_pkg","FP4_2_pkg"}, {"../../../../include"}, 16, 0);
        ok &= VerilatorCompile(__FILE__, "FpConverterFP4_2_FP16_5", {"Predef_pkg","FP4_2_pkg","FP16_5_pkg"}, {"../../../../include"}, 16, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && std::system((std::string("FpConverterFP32_8_FP16_5_16_1/obj_dir/VFpConverterFP32_8_FP16_5 32 8 16 5 16 1") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP16_5_FP32_8_16_0/obj_dir/VFpConverterFP16_5_FP32_8 16 5 32 8 16 0") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP32_8_FP16_8_16_1/obj_dir/VFpConverterFP32_8_FP16_8 32 8 16 8 16 1") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP16_8_FP32_8_16_0/obj_dir/VFpConverterFP16_8_FP32_8 16 8 32 8 16 0") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP16_5_FP16_8_16_1/obj_dir/VFpConverterFP16_5_FP16_8 16 5 16 8 16 1") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP16_8_FP16_5_16_0/obj_dir/VFpConverterFP16_8_FP16_5 16 8 16 5 16 0") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP32_8_FP19_8_16_1/obj_dir/VFpConverterFP32_8_FP19_8 32 8 19 8 16 1") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP19_8_FP16_5_16_0/obj_dir/VFpConverterFP19_8_FP16_5 19 8 16 5 16 0") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP16_5_FP8_4_16_1/obj_dir/VFpConverterFP16_5_FP8_4 16 5 8 4 16 1") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP8_4_FP4_2_16_0/obj_dir/VFpConverterFP8_4_FP4_2 8 4 4 2 16 0") + (debug?" --debug":"")).c_str()) == 0
            && std::system((std::string("FpConverterFP4_2_FP16_5_16_1/obj_dir/VFpConverterFP4_2_FP16_5 4 2 16 5 16 1") + (debug?" --debug":"")).c_str()) == 0
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    if (positional.size() >= 6) {
        size_t in_width = std::stoull(positional[0]);
        size_t in_exp = std::stoull(positional[1]);
        size_t out_width = std::stoull(positional[2]);
        size_t out_exp = std::stoull(positional[3]);
        size_t steps = std::stoull(positional[4]);
        size_t round = std::stoull(positional[5]);
        if (in_width == 32 && in_exp == 8 && out_width == 16 && out_exp == 5 && steps == 16 && round == 1) {
            return !(ok && run_reference_tests() && TestFpConverter<FP32E8,FP16E5,16,1>(debug).run());
        }
        if (in_width == 16 && in_exp == 5 && out_width == 32 && out_exp == 8 && steps == 16 && round == 0) {
            return !(ok && run_reference_tests() && TestFpConverter<FP16E5,FP32E8,16,0>(debug).run());
        }
        if (in_width == 32 && in_exp == 8 && out_width == 16 && out_exp == 8 && steps == 16 && round == 1) {
            return !(ok && run_reference_tests() && TestFpConverter<FP32E8,BF16E8,16,1>(debug).run());
        }
        if (in_width == 16 && in_exp == 8 && out_width == 32 && out_exp == 8 && steps == 16 && round == 0) {
            return !(ok && run_reference_tests() && TestFpConverter<BF16E8,FP32E8,16,0>(debug).run());
        }
        if (in_width == 16 && in_exp == 5 && out_width == 16 && out_exp == 8 && steps == 16 && round == 1) {
            return !(ok && run_reference_tests() && TestFpConverter<FP16E5,BF16E8,16,1>(debug).run());
        }
        if (in_width == 16 && in_exp == 8 && out_width == 16 && out_exp == 5 && steps == 16 && round == 0) {
            return !(ok && run_reference_tests() && TestFpConverter<BF16E8,FP16E5,16,0>(debug).run());
        }
        if (in_width == 32 && in_exp == 8 && out_width == 19 && out_exp == 8 && steps == 16 && round == 1) {
            return !(ok && run_reference_tests() && TestFpConverter<FP32E8,TF19E8,16,1>(debug).run());
        }
        if (in_width == 19 && in_exp == 8 && out_width == 16 && out_exp == 5 && steps == 16 && round == 0) {
            return !(ok && run_reference_tests() && TestFpConverter<TF19E8,FP16E5,16,0>(debug).run());
        }
        if (in_width == 16 && in_exp == 5 && out_width == 8 && out_exp == 4 && steps == 16 && round == 1) {
            return !(ok && run_reference_tests() && TestFpConverter<FP16E5,FP8E4,16,1>(debug).run());
        }
        if (in_width == 8 && in_exp == 4 && out_width == 4 && out_exp == 2 && steps == 16 && round == 0) {
            return !(ok && run_reference_tests() && TestFpConverter<FP8E4,FP4E2,16,0>(debug).run());
        }
        if (in_width == 4 && in_exp == 2 && out_width == 16 && out_exp == 5 && steps == 16 && round == 1) {
            return !(ok && run_reference_tests() && TestFpConverter<FP4E2,FP16E5,16,1>(debug).run());
        }
        std::print("Unsupported FpConverter test parameters: IN={} E{} OUT={} E{} STEPS={} ROUND={}\n", in_width, in_exp, out_width, out_exp, steps, round);
        return 1;
    }

    ok = ok && run_reference_tests();
    ok = ok && TestFpConverter<FP32E8,FP16E5,16,1>(debug).run();
    ok = ok && TestFpConverter<FP16E5,FP32E8,16,0>(debug).run();
    ok = ok && TestFpConverter<FP32E8,BF16E8,16,1>(debug).run();
    ok = ok && TestFpConverter<BF16E8,FP32E8,16,0>(debug).run();
    ok = ok && TestFpConverter<FP16E5,BF16E8,16,1>(debug).run();
    ok = ok && TestFpConverter<BF16E8,FP16E5,16,0>(debug).run();
    ok = ok && TestFpConverter<FP32E8,TF19E8,16,1>(debug).run();
    ok = ok && TestFpConverter<TF19E8,FP16E5,16,0>(debug).run();
    ok = ok && TestFpConverter<FP16E5,FP8E4,16,1>(debug).run();
    ok = ok && TestFpConverter<FP8E4,FP4E2,16,0>(debug).run();
    ok = ok && TestFpConverter<FP4E2,FP16E5,16,1>(debug).run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
