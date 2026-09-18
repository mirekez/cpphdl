#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <type_traits>

using namespace cpphdl;

// A small registered FIFO-like leaf: type selects the module specialization,
// while all three value arguments must remain ordinary SV parameters.
template<uint64_t FallThrough, uint64_t DataWidth, uint64_t Depth, typename T>
class TypeWidthFifo : public Module
{
    reg<T> value;
public:
    _PORT(T) data_in;
    _PORT(T) data_out;

    void _assign() { data_out = _ASSIGN_REG(value); }
    void _work(bool reset)
    {
        value._next = data_in() + FallThrough + DataWidth + Depth;
        if (reset) {
            value._next = 0;
        }
    }
    void _strobe() { value.strobe(); }
};

template<unsigned AxiIdWidth = 4, unsigned MaxTrans = 4>
class TemplateTypeWidth : public Module
{
    using id_t = logic<(uint64_t)(((uint64_t)(AxiIdWidth) & ((1ull << 32) - 1ull)))>;
    static_assert(std::is_same_v<id_t, logic<AxiIdWidth>>);

    TypeWidthFifo<1, 32, MaxTrans, logic<4>> literal;
    TypeWidthFifo<1, 32, MaxTrans, logic<AxiIdWidth>> symbolic;
    TypeWidthFifo<static_cast<uint64_t>((0b1)), 32, MaxTrans, id_t> masked;
    TypeWidthFifo<1, 32, MaxTrans + 3, id_t> lanes[2];
    TypeWidthFifo<1, 32, MaxTrans, logic<5>> different;
public:
    _PORT(logic<8>) data_in;
    _PORT(logic<4>) literal_out;
    _PORT(logic<AxiIdWidth>) parameter_out, masked_out, lane0_out, lane1_out;
    _PORT(logic<5>) different_out;

    void _assign()
    {
        size_t i;
        literal.data_in = _ASSIGN(data_in());
        symbolic.data_in = _ASSIGN(data_in());
        masked.data_in = _ASSIGN(data_in());
        different.data_in = _ASSIGN(data_in());
        literal_out = _ASSIGN(literal.data_out());
        parameter_out = _ASSIGN(symbolic.data_out());
        masked_out = _ASSIGN(masked.data_out());
        different_out = _ASSIGN(different.data_out());
        lane0_out = _ASSIGN(lanes[0].data_out());
        lane1_out = _ASSIGN(lanes[1].data_out());
        literal._assign();
        symbolic._assign();
        masked._assign();
        different._assign();
        for (i = 0; i < 2; ++i) {
            lanes[i].data_in = _ASSIGN_I(data_in() + i);
            lanes[i]._assign();
        }
    }
    void _work(bool reset)
    {
        size_t i;
        literal._work(reset);
        symbolic._work(reset);
        masked._work(reset);
        different._work(reset);
        for (i = 0; i < 2; ++i) {
            lanes[i]._work(reset);
        }
    }
    void _strobe()
    {
        size_t i;
        literal._strobe();
        symbolic._strobe();
        masked._strobe();
        different._strobe();
        for (i = 0; i < 2; ++i) {
            lanes[i]._strobe();
        }
    }
};

template class TemplateTypeWidth<4, 4>;
template class TemplateTypeWidth<4, 7>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = 0;

static bool check_generated()
{
    const auto read = [](const char* name) {
        std::ifstream in(std::string("generated/") + name + ".sv");
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto top = read("TemplateTypeWidth");
    const auto narrow = read("TypeWidthFifologic4m1_0");
    const auto wide = read("TypeWidthFifologic5m1_0");
    return !narrow.empty() && !wide.empty()
        && top.find("TypeWidthFifologic4m1_0") != std::string::npos
        && top.find("TypeWidthFifologic5m1_0") != std::string::npos
        && top.find("MaxTrans") != std::string::npos
        && narrow.find("parameter FallThrough") != std::string::npos
        && narrow.find("parameter DataWidth") != std::string::npos
        && narrow.find("parameter Depth") != std::string::npos
        && narrow.find("wire[4-1:0] data_out") != std::string::npos
        && wide.find("wire[5-1:0] data_out") != std::string::npos
        && narrow.find("data_in()") == std::string::npos
        && top.find("unknown:") == std::string::npos;
}

#ifndef VERILATOR
struct NativeTypeWidthTest
{
    // Deliberately override the parent's default: freezing value parameters to
    // the first specialization must not accidentally pass this regression.
    TemplateTypeWidth<4, 7> dut;
    logic<8> input = 0;
    void _assign()
    {
        dut.data_in = _ASSIGN_REG(input);
        dut._assign();
    }
};
#endif

int main(int argc, char** argv)
{
#ifdef VERILATOR
    Verilated::commandArgs(argc, argv);
    VERILATOR_MODEL dut;
#else
    NativeTypeWidthTest test;
    auto& dut = test.dut;
    auto& input = test.input;
    test._assign();
    if (!check_generated()) {
        std::cerr << "Type-width specialization RTL structure failed\n";
        return 1;
    }
    if (!(argc > 1 && std::strcmp(argv[1], "--noveril") == 0)) {
        if (!VerilatorCompile(__FILE__, "TemplateTypeWidth",
                {"Predef_pkg", "TypeWidthFifologic4m1_0", "TypeWidthFifologic5m1_0"},
                {}, 4, 7)
            || std::system("TemplateTypeWidth_4_7/obj_dir/VTemplateTypeWidth") != 0) {
            return 1;
        }
    }
#endif
    for (unsigned cycle = 0; cycle < 512; ++cycle) {
        const bool reset = cycle == 0 || cycle == 257;
        const unsigned value = cycle & 255;
#ifdef VERILATOR
        dut.data_in = value;
        dut.reset = reset;
        dut.clk = 0;
        dut.eval();
        dut.clk = 1;
        dut.eval();
        const unsigned outputs[] = {dut.literal_out, dut.parameter_out,
            dut.masked_out, dut.lane0_out, dut.lane1_out, dut.different_out};
#else
        input = value;
        dut._work(reset);
        dut._strobe();
        ++_system_clock;
        const unsigned outputs[] = {(unsigned)dut.literal_out(), (unsigned)dut.parameter_out(),
            (unsigned)dut.masked_out(), (unsigned)dut.lane0_out(),
            (unsigned)dut.lane1_out(), (unsigned)dut.different_out()};
#endif
        for (unsigned i = 0; i < 6; ++i) {
            const unsigned expected = reset ? 0 : ((value + 1 + 32 + 7
                + ((i == 3 || i == 4) ? 3 : 0) + (i == 4)) & (i == 5 ? 31 : 15));
            unsigned observed = outputs[i];
#ifndef VERILATOR
            // Native non-byte-width logic can carry padding after arithmetic;
            // compare only the bits present on the declared hardware port.
            observed &= i == 5 ? 31 : 15;
#endif
            if (observed != expected) {
                std::cerr << "Type-width output " << i << " cycle " << cycle
                    << ": got " << observed << ", expected " << expected << '\n';
                return 1;
            }
        }
#ifdef VERILATOR
        ++_system_clock;
#endif
    }
    std::cout << "Type-width specialization: 512 cycles passed\n";
    return 0;
}
#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
