#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#define CPPHDL_DISABLE_RAW_FIELD_FORMATTER
#include "cpphdl.h"
#include <print>

using namespace cpphdl;

#include <cstdint>
#include <cstring>
#include <type_traits>

template<size_t W>
using fp_raw_t = std::conditional_t<(W<=8), uint8_t,std::conditional_t<(W<=16), uint16_t,std::conditional_t<(W<=32), uint32_t,uint64_t>>>;

template<size_t W, size_t EW, typename RAW, bool PADDED>
struct FPBits;

template<size_t W, size_t EW, typename RAW>
struct FPBits<W, EW, RAW, false>
{
    static constexpr size_t MANT_WIDTH = W-EW-1;
    union {
        RAW raw : W;
        struct {
            RAW mantissa : MANT_WIDTH;
            RAW exponent : EW;
            RAW sign : 1;
        }__attribute__((packed));
    };
};

template<size_t W, size_t EW, typename RAW>
struct FPBits<W, EW, RAW, true>
{
    static constexpr size_t MANT_WIDTH = W-EW-1;
    static constexpr size_t PAD_WIDTH = sizeof(RAW) * 8 - W;
    union {
        RAW raw;
        struct {
            RAW mantissa : MANT_WIDTH;
            RAW exponent : EW;
            RAW sign : 1;
            RAW _pad : PAD_WIDTH;
        }__attribute__((packed));
    };
};

template<size_t W, size_t EW>
struct FP : public FPBits<W, EW, fp_raw_t<W>, (sizeof(fp_raw_t<W>) * 8 != W)>
{
    static constexpr size_t WIDTH = W;
    static constexpr size_t EXP_WIDTH = EW;
    static constexpr size_t MANT_WIDTH = W-EW-1;
    using RAW = fp_raw_t<W>;
    using Storage = FPBits<W, EW, fp_raw_t<W>, (sizeof(fp_raw_t<W>) * 8 != W)>;
    using Storage::raw;
    using Storage::mantissa;
    using Storage::exponent;
    using Storage::sign;
    static constexpr uint64_t EXP_MAX = (1ULL << EXP_WIDTH) - 1ULL;
    static constexpr uint64_t MANT_MAX = (MANT_WIDTH == 64) ? ~0ULL : ((1ULL << MANT_WIDTH) - 1ULL);
    static constexpr int EXP_BIAS = (1 << (EXP_WIDTH - 1)) - 1;

    static_assert(WIDTH <= 64, "WIDTH too large");
    static_assert(EXP_WIDTH <= WIDTH-2, "EXP_WIDTH too large");
    static_assert(EXP_WIDTH <= 11, "EXP_WIDTH too large");
    static_assert(MANT_WIDTH <= 52, "MANT_WIDTH too large");

    static FP make(bool sign_in, uint64_t exponent_in, uint64_t mantissa_in)
    {
        FP out;
        out.raw = 0;
        out.sign = sign_in;
        out.exponent = exponent_in;
        out.mantissa = mantissa_in;
        return out;
    }

    static FP zero(bool sign_in = false)
    {
        return make(sign_in, 0, 0);
    }

    static FP inf(bool sign_in = false)
    {
        return make(sign_in, EXP_MAX, 0);
    }

    static FP nan()
    {
        return make(false, EXP_MAX, 1);
    }

    static FP one(bool sign_in = false)
    {
        return make(sign_in, EXP_BIAS, 0);
    }

    static FP min_normal(bool sign_in = false)
    {
        return make(sign_in, 1, 0);
    }

    static FP max_finite(bool sign_in = false)
    {
        return make(sign_in, EXP_MAX - 1, MANT_MAX);
    }

    static FP from_raw(uint64_t raw_in)
    {
        FP out;
        out.raw = (RAW)raw_in;
        return out;
    }

    template<typename TYPE>
    void convert(TYPE& to_out)
    {
        uint64_t mant_work;
        uint64_t mant_keep;
        uint64_t round_bits;
        uint64_t round_half;
        uint64_t dst_mant_max;
        uint64_t exponent_work;
        unsigned shift;

        to_out.raw = 0;
        to_out.sign = sign;

        if (exponent == 0) {
            to_out.exponent = 0;
            to_out.mantissa = 0;
        }
        else if (exponent == EXP_MAX) {
            to_out.exponent = TYPE::EXP_MAX;
            to_out.mantissa = mantissa ? 1 : 0;
        }
        else {
            if constexpr (EXP_BIAS >= TYPE::EXP_BIAS) {
                if ((uint64_t)exponent <= (uint64_t)(EXP_BIAS - TYPE::EXP_BIAS)) {
                    exponent_work = 0;
                }
                else {
                    exponent_work = (uint64_t)exponent - (uint64_t)(EXP_BIAS - TYPE::EXP_BIAS);
                }
            }
            else {
                exponent_work = (uint64_t)exponent + (uint64_t)(TYPE::EXP_BIAS - EXP_BIAS);
            }
            if (exponent_work == 0) {
                to_out.exponent = 0;
                to_out.mantissa = 0;
            }
            else if (exponent_work >= TYPE::EXP_MAX) {
                to_out.exponent = TYPE::EXP_MAX;
                to_out.mantissa = 0;
            }
            else {
                dst_mant_max = TYPE::MANT_MAX;
                if constexpr (TYPE::MANT_WIDTH >= MANT_WIDTH) {
                    mant_work = (uint64_t)mantissa << (TYPE::MANT_WIDTH - MANT_WIDTH);
                }
                else if constexpr (MANT_WIDTH - TYPE::MANT_WIDTH < 64) {
                    shift = MANT_WIDTH - TYPE::MANT_WIDTH;
                    mant_keep = (uint64_t)mantissa >> shift;
                    round_bits = (uint64_t)mantissa & ((1ULL << shift) - 1ULL);
                    round_half = 1ULL << (shift - 1);
                    // Round-to-nearest-even: round up only above half, or exactly half
                    // when the retained mantissa is odd.
                    if (round_bits > round_half || (round_bits == round_half && (mant_keep & 1ULL))) {
                        ++mant_keep;
                    }
                    mant_work = mant_keep;
                    if (mant_work > dst_mant_max) {
                        mant_work = 0;
                        ++exponent_work;
                    }
                }
                else {
                    mant_work = 0;
                    if (mantissa) {
                        mant_work = 1;
                    }
                }
                if (exponent_work >= TYPE::EXP_MAX) {
                    to_out.exponent = TYPE::EXP_MAX;
                    to_out.mantissa = 0;
                }
                else {
                    to_out.exponent = exponent_work;
                    to_out.mantissa = mant_work;
                }
            }
        }

#ifndef SYNTHESIS
//        std::print("{}({}) => {}({})\n", *this, this->to_double(), to, to.to_double());
#endif
    }

#ifndef SYNTHESIS
    template<typename TYPE>
    TYPE converted() const
    {
        TYPE out;
        const_cast<FP*>(this)->convert(out);
        return out;
    }

    double to_double() const
    {
        uint64_t ret = 0;
        uint64_t exp = 0;
        uint64_t mant = 0;
        ret |= (uint64_t)sign << 63;
        if (exponent == EXP_MAX) {
            exp = (1ULL << 11) - 1ULL;
            mant = mantissa ? (1ULL << 51) : 0;
        }
        else if (exponent != 0) {
            exp = (uint64_t)((int)exponent - EXP_BIAS + 1023);
            mant = (uint64_t)mantissa << (52 - MANT_WIDTH);
        }
        ret |= exp << 52;
        ret |= mant;
        double out;
        std::memcpy(&out, &ret, sizeof(out));
        return out;
    }

    void from_double(double in)
    {
        uint64_t bits = 0;
        uint64_t mant64;
        uint64_t mant_keep;
        uint64_t round_bits;
        uint64_t round_half;
        int exp64;
        int exponent_work;

        std::memcpy(&bits, &in, sizeof(bits));
        sign = bits >> 63;
        exp64 = (int)((bits >> 52) & ((1ULL << 11) - 1ULL));
        mant64 = bits & ((1ULL << 52) - 1ULL);

        raw = 0;
        sign = bits >> 63;
        if (exp64 == 0) {
            exponent = 0;
            mantissa = 0;
            return;
        }
        if (exp64 == (int)((1ULL << 11) - 1ULL)) {
            exponent = EXP_MAX;
            mantissa = mant64 ? 1 : 0;
            return;
        }

        exponent_work = exp64 - 1023 + EXP_BIAS;
        if (exponent_work <= 0) {
            exponent = 0;
            mantissa = 0;
            return;
        }
        if (exponent_work >= (int)EXP_MAX) {
            exponent = EXP_MAX;
            mantissa = 0;
            return;
        }

        if (MANT_WIDTH >= 52) {
            mantissa = mant64 << (MANT_WIDTH - 52);
        }
        else {
            const unsigned shift = 52 - MANT_WIDTH;
            mant_keep = mant64 >> shift;
            round_bits = mant64 & ((1ULL << shift) - 1ULL);
            round_half = 1ULL << (shift - 1);
            if (round_bits > round_half || (round_bits == round_half && (mant_keep & 1ULL))) {
                ++mant_keep;
            }
            if (mant_keep > MANT_MAX) {
                mant_keep = 0;
                ++exponent_work;
                if (exponent_work >= (int)EXP_MAX) {
                    exponent = EXP_MAX;
                    mantissa = 0;
                    return;
                }
            }
            mantissa = mant_keep;
        }
        exponent = exponent_work;
    }

    bool cmp(double ref, double threshold) const
    {
        return to_double() >= ref - threshold && to_double() <= ref + threshold;
    }
#endif
};

using FP16E5 = FP<16,5>;
using BF16E8 = FP<16,8>;
using FP32E8 = FP<32,8>;
using TF19E8 = FP<19,8>;
using FP8E4 = FP<8,4>;
using FP4E2 = FP<4,2>;

template<size_t W, size_t EW>
struct std::formatter<FP<W,EW>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    template <typename FormatContext>
    auto format(const FP<W,EW>& fp, FormatContext& ctx) const
    {
        auto out = ctx.out();
        if constexpr (W <= 8) {
            out = std::format_to(out, "{:02x}", (uint64_t)fp.raw);
        }
        else if constexpr (W <= 16) {
            out = std::format_to(out, "{:04x}", (uint64_t)fp.raw);
        }
        else {
            out = std::format_to(out, "{:08x}", (uint64_t)fp.raw);
        }
        return out;
    }
};

// CppHDL MODEL /////////////////////////////////////////////////////////

template<typename STYPE, typename DTYPE, size_t LENGTH, bool USE_REG>
class FpConverter : public Module
{
public:
    _PORT(array<STYPE,LENGTH>)    data_in;
    _PORT(array<DTYPE,LENGTH>)    data_out = _ASSIGN( USE_REG ? out_reg : conv_comb_func() );

private:
    reg<array<DTYPE,LENGTH>> out_reg;

    array<DTYPE,LENGTH> conv_comb;
    array<DTYPE,LENGTH>& conv_comb_func()
    {
        DTYPE converted;
        if constexpr (LENGTH > 0) { data_in()[0].convert(converted); conv_comb[0] = converted; }
        if constexpr (LENGTH > 1) { data_in()[1].convert(converted); conv_comb[1] = converted; }
        if constexpr (LENGTH > 2) { data_in()[2].convert(converted); conv_comb[2] = converted; }
        if constexpr (LENGTH > 3) { data_in()[3].convert(converted); conv_comb[3] = converted; }
        if constexpr (LENGTH > 4) { data_in()[4].convert(converted); conv_comb[4] = converted; }
        if constexpr (LENGTH > 5) { data_in()[5].convert(converted); conv_comb[5] = converted; }
        if constexpr (LENGTH > 6) { data_in()[6].convert(converted); conv_comb[6] = converted; }
        if constexpr (LENGTH > 7) { data_in()[7].convert(converted); conv_comb[7] = converted; }
        if constexpr (LENGTH > 8) { data_in()[8].convert(converted); conv_comb[8] = converted; }
        if constexpr (LENGTH > 9) { data_in()[9].convert(converted); conv_comb[9] = converted; }
        if constexpr (LENGTH > 10) { data_in()[10].convert(converted); conv_comb[10] = converted; }
        if constexpr (LENGTH > 11) { data_in()[11].convert(converted); conv_comb[11] = converted; }
        if constexpr (LENGTH > 12) { data_in()[12].convert(converted); conv_comb[12] = converted; }
        if constexpr (LENGTH > 13) { data_in()[13].convert(converted); conv_comb[13] = converted; }
        if constexpr (LENGTH > 14) { data_in()[14].convert(converted); conv_comb[14] = converted; }
        if constexpr (LENGTH > 15) { data_in()[15].convert(converted); conv_comb[15] = converted; }
        return conv_comb;
    }

public:

    void _work(bool reset)
    {
        if (USE_REG) {
            out_reg._next = conv_comb_func();
        }

        if (reset) {
            return;
        }

        if (debugen_in) {
            std::print("{:s}: input: {}, output: {}\n", __inst_name, data_in(), data_out());
        }
    }

    void _strobe()
    {
        out_reg.strobe();
    }

    void _assign() {}

    bool     debugen_in;
};
/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

template class FpConverter<FP32E8,FP16E5,16,1>;
template class FpConverter<FP16E5,FP32E8,16,0>;
template class FpConverter<FP32E8,BF16E8,16,1>;
template class FpConverter<BF16E8,FP32E8,16,0>;
template class FpConverter<FP16E5,BF16E8,16,1>;
template class FpConverter<BF16E8,FP16E5,16,0>;
template class FpConverter<FP32E8,TF19E8,16,1>;
template class FpConverter<TF19E8,FP16E5,16,0>;
template class FpConverter<FP16E5,FP8E4,16,1>;
template class FpConverter<FP8E4,FP4E2,16,0>;
template class FpConverter<FP4E2,FP16E5,16,1>;

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

    reg<array<STYPE,LENGTH>> out_reg;
    reg<array<DTYPE,LENGTH>> expected1;
    reg<array<DTYPE,LENGTH>> expected2;
    reg<u1> can_check1;
    reg<u1> can_check2;
    bool error;

    array<DTYPE,LENGTH>    read_data;  // to support Verilator

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
        std::print("CppHDL TestFpConverter, STYPE: {}, DTYPE: {}, LENGTH: {}, USE_REG: {}...", typeid(STYPE).name(), typeid(DTYPE).name(), LENGTH, USE_REG);
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
