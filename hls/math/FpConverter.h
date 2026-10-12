#pragma once
#define CPPHDL_DISABLE_RAW_FIELD_FORMATTER
#include <cpphdl.h>
#include <print>
#include <cstdint>
#include <cstring>
#include <type_traits>
using namespace cpphdl;

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

// C++HDL MODEL /////////////////////////////////////////////////////////

template<typename STYPE, typename DTYPE, size_t LENGTH, bool USE_REG>
class FpConverter : public Module
{
public:
    _PORT(array<LENGTH,STYPE>)    data_in;
    _PORT(array<LENGTH,DTYPE>)    data_out = _ASSIGN( USE_REG ? out_reg : conv_comb_func() );

private:
    reg<array<LENGTH,DTYPE>> out_reg;

    array<LENGTH,DTYPE> conv_comb;
    array<LENGTH,DTYPE>& conv_comb_func()
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
