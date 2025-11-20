#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

// we

using namespace cpphdl;

#include <cstdint>
#include <type_traits>

template<size_t W, size_t EW>
struct FP
{
    static constexpr size_t WIDTH = W;
    static constexpr size_t EXP_WIDTH = EW;
    static constexpr size_t MANT_WIDTH = W-EW-1;
    using RAW = std::conditional_t<(WIDTH<=8), uint8_t,std::conditional_t<(WIDTH<=16), uint16_t,std::conditional_t<(WIDTH<=32), uint32_t,uint64_t>>>;

    union {
        RAW raw : WIDTH;
        struct {
            RAW mantissa : MANT_WIDTH;
            RAW exponent : EXP_WIDTH;
            RAW sign : 1;
        }__attribute__((packed));
    };

    static_assert(WIDTH <= 64, "WIDTH too large");
    static_assert(EXP_WIDTH <= WIDTH-2, "EXP_WIDTH too large");
    static_assert(EXP_WIDTH <= 11, "EXP_WIDTH too large");
    static_assert(MANT_WIDTH <= 52, "MANT_WIDTH too large");

    template<typename TYPE>
    void convert(TYPE& to_out)
    {
        to_out.sign = sign;
        to_out.exponent = exponent - ((1ULL<<(EXP_WIDTH-1))-1) + ((1ULL<<(to_out.EXP_WIDTH-1))-1);

        if (to_out.MANT_WIDTH >= MANT_WIDTH) {
            to_out.mantissa = mantissa << (to_out.MANT_WIDTH - MANT_WIDTH);
        }
        else {
            if (MANT_WIDTH - to_out.MANT_WIDTH < MANT_WIDTH) {
                to_out.mantissa = mantissa >> (MANT_WIDTH - to_out.MANT_WIDTH);
            }
            else {
                to_out.mantissa = 0;
            }
        }

        if (exponent == ((1ULL<<EXP_WIDTH)-1)
        || (exponent > ((1ULL<<(EXP_WIDTH-1))-1) && exponent - ((1ULL<<(EXP_WIDTH-1))-1) > (1ULL<<(to_out.EXP_WIDTH-1)))) {  // overflow
            to_out.exponent = ((1ULL<<to_out.EXP_WIDTH)-1);
            to_out.mantissa = 0;
        }
        if (exponent == 0
        || (exponent < ((1ULL<<(EXP_WIDTH-1))-1) && ((1ULL<<(EXP_WIDTH-1))-1) - exponent >= (1ULL<<(to_out.EXP_WIDTH-1)))) {  // underflow
            to_out.exponent = 0;
            to_out.mantissa = 0;
        }

        // TODO: rounding

#ifndef SYNTHESIS
//        std::print("{}({}) => {}({})\n", *this, this->to_double(), to, to.to_double());
#endif
    }

#ifndef SYNTHESIS
    double to_double()
    {
        uint64_t ret = 0;
        ret |= (uint64_t)sign << 63;
        uint16_t exp = exponent - ((1ULL<<(EXP_WIDTH-1))-1) + ((1ULL<<(11-1))-1);
        if (exponent == ((1ULL<<EXP_WIDTH)-1)) {
            exp = ((1ULL<<11)-1);
        }
        if (exponent == 0) {
            exp = 0;
        }
        ret |= (uint64_t)exp << 52;
        ret |= (uint64_t)mantissa << (52 - MANT_WIDTH);
        return *(double*)&ret;
    }

    void from_double(double in)
    {
        mantissa = ((*(uint64_t*)&in)&((1ULL<<52)-1)) >> (52 - MANT_WIDTH);
        uint64_t exp = (((*(uint64_t*)&in)&~(1ULL<<63))>>52);
        exponent = exp - (((1ULL<<(11-1))-1)) + ((1ULL<<(EXP_WIDTH-1))-1);
        if (exp == (((1ULL<<11)-1)) || (exp > (((1ULL<<(11-1))-1)) && exp - (((1ULL<<(11-1))-1)) > (1ULL<<(EXP_WIDTH-1)))) {
            exponent = ((1ULL<<EXP_WIDTH)-1);
            mantissa = 0;
        }
        if (exp == 0 || (exp < (((1ULL<<(11-1))-1)) && (((1ULL<<(11-1))-1)) - exp >= (1ULL<<(EXP_WIDTH-1)))) {
            exponent = 0;
            mantissa = 0;
        }
        sign = (*(uint64_t*)&in)>>63;
    }

    bool cmp(double ref, double threshold)
    {
        return to_double() >= ref - threshold && to_double() <= ref + threshold;
    }
#endif
};

template<size_t W, size_t EW>
struct std::formatter<FP<W,EW>>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
    template <typename FormatContext>
    auto format(const FP<W,EW>& fp, FormatContext& ctx) const
    {
        auto out = ctx.out();
        if (W == 16) {
            out = std::format_to(out, "{:04x}", fp.raw);
        }
        else {
            out = std::format_to(out, "{:08x}", fp.raw);
        }
        return out;
    }
};

// C++HDL MODEL /////////////////////////////////////////////////////////
template<typename STYPE, typename DTYPE, size_t LENGTH, bool USE_REG>
class FpConverter : public Module
{
    array<DTYPE,LENGTH> out_comb;
    reg<array<DTYPE,LENGTH>> out_reg;

    DTYPE conv_comb;

public:
    array<STYPE,LENGTH>    *data_in  = nullptr;
    array<DTYPE,LENGTH>    *data_out = &out_comb;

    bool     debugen_in;

    void connect() {}

    size_t i;
    array<DTYPE,LENGTH>& conv_comb_func()
    {
        if (!USE_REG) {
            for (i=0; i < LENGTH; ++i) {
                (*data_in)[i].convert(out_comb[i]);
            }
        }
        else {
            out_comb = out_reg;
        }
        return out_comb;
    }

    void work(bool clk, bool reset)
    {
        if (!clk) return;

        if (USE_REG) {
            for (i=0; i < LENGTH; ++i) {
                (*data_in)[i].convert(out_reg.next[i]);
            }
        }

        if (reset) {
            return;
        }

        if (debugen_in) {
            std::print("{:s}: input: {}, output: {}\n", __inst_name, *data_in, *data_out);
        }
    }

    void strobe()
    {
        out_reg.strobe();
    }

    void comb()
    {
    }
};
/////////////////////////////////////////////////////////////////////////

// C++HDL INLINE TEST ///////////////////////////////////////////////////

template class FpConverter<FP<32,8>,FP<16,5>,8,1>;
template class FpConverter<FP<16,5>,FP<32,8>,8,0>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

template<typename STYPE, typename DTYPE, size_t LENGTH, bool USE_REG>
class TestFpConverter : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL converter;
#else
    FpConverter<STYPE,DTYPE,LENGTH,USE_REG> converter;
#endif

    double refs[LENGTH];
    reg<array<STYPE,LENGTH>> out_reg;
    reg<array<double,LENGTH>> was_refs1;
    reg<array<double,LENGTH>> was_refs2;
    reg<u1> can_check1;
    reg<u1> can_check2;
    bool error;

    size_t i;

public:
    array<DTYPE,LENGTH>    *data_in  = nullptr;
    array<STYPE,LENGTH>    *data_out = &out_reg;

    bool             debugen_in;

    TestFpConverter(bool debug)
    {
        debugen_in = debug;
    }

    ~TestFpConverter()
    {
    }

    void connect()
    {
#ifndef VERILATOR
        converter.__inst_name = __inst_name + "/converter";

        converter.data_in      = data_out;
        converter.debugen_in   = debugen_in;
        converter.connect();

        data_in           = converter.data_out;
#endif
    }

    void work(bool clk, bool reset)
    {
#ifndef VERILATOR
        converter.work(clk, reset);
#else
        memcpy(&converter.data_in.m_storage, data_out, sizeof(converter.data_in.m_storage));
        converter.debugen_in    = debugen_in;

        data_in           = (array<DTYPE,LENGTH>*) &converter.data_out.m_storage;

        converter.clk = clk;
        converter.reset = reset;
        converter.eval();  // eval of verilator should be in the end
#endif

        if (reset) {
            error = false;
            can_check1.clr();
            can_check2.clr();
            return;
        }

        if (!clk) {  // all checks on negedge edge
            for (i=0; i < LENGTH; ++i) {
                if (!reset && ((!USE_REG && can_check1 && !(*data_in)[i].cmp(was_refs1[i], 0.1))
                             || (USE_REG && can_check2 && !(*data_in)[i].cmp(was_refs2[i], 0.1))) ) {
                    std::print("{:s} ERROR: {}({}) was read instead of {}\n",
                        __inst_name,
                        (*data_in)[i].to_double(),
                        (*data_in)[i],
                        USE_REG?was_refs2[i]:was_refs1[i]);
                    error = true;
                }
            }
            return;
        }

        for (i=0; i < LENGTH; ++i) {
            refs[i] = ((double)random() - RAND_MAX/2) / (RAND_MAX/2);
            out_reg.next[i].from_double(refs[i]);
        }
        was_refs1.next = refs;
        was_refs2.next = was_refs1;
        can_check1.next = 1;
        can_check2.next = can_check1;
    }

    void strobe()
    {
#ifndef VERILATOR
        converter.strobe();
#endif

        out_reg.strobe();
        was_refs1.strobe();
        was_refs2.strobe();
        can_check1.strobe();
        can_check2.strobe();
    }

    void comb()
    {
#ifndef VERILATOR
        converter.conv_comb_func();
        converter.comb();
#endif
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
        connect();
        work(0, 1);
        work(1, 1);
        int cycles = 100000;
        int clk = 0;
        while (--cycles && !error) {
            comb();
            work(clk, 0);
            strobe();
            clk = !clk;
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
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        debug = true;
    }
    if (argc > 2 && strcmp(argv[2], "--noveril") == 0) {
        noveril = true;
    }
    int only = -1;
    if (argc > 1 && argv[argc-1][0] != '-') {
        only = atoi(argv[argc-1]);
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        ok &= VerilatorCompile("FpConverter.cpp", "FpConverterFP32_8_FP16_5", {"FP16_5_pkg","FP32_8_pkg"}, 8, 1);
        ok &= VerilatorCompile("FpConverter.cpp", "FpConverterFP16_5_FP32_8", {"FP16_5_pkg","FP32_8_pkg"}, 8, 0);
        std::cout << "Executing tests... ===========================================================================\n";
        std::system((std::string("FpConverterFP32_8_FP16_5_8_1/obj_dir/VFpConverterFP32_8_FP16_5") + (debug?" --debug":"") + " 0").c_str());
        std::system((std::string("FpConverterFP16_5_FP32_8_8_0/obj_dir/VFpConverterFP16_5_FP32_8") + (debug?" --debug":"") + " 1").c_str());
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
    && ((only != -1 && only != 0) || TestFpConverter<FP<32,8>,FP<16,5>,8,1>(debug).run())
    && ((only != -1 && only != 1) || TestFpConverter<FP<16,5>,FP<32,8>,8,0>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
