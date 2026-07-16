#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <stdint.h>

using namespace cpphdl;

#define log2 clog2

struct UUU
{
    uint8_t aaaaaaaa:5,
            aaaaaaa1:3;
    uint8_t bbbbbbbb:4,
            cccccccc:4;
    uint8_t ddddddddd:4,
            eeeeeeeee:4;
    uint8_t FFFFFFFFFFFFFFFF:3,
            gggggggggg:2,
            hhhhhhhhhhh:3;
};

#define FILL_UUU UUU{(uint8_t)clog2(AAAAAAAA), (uint8_t)clog2(AAAAAAA1)-5, BBBBBBBB, CCCCCCCCC, DDDDDDDDD>>2, EEEEE>>2, \
                     (uint8_t)log2(FFFFFFFFFFFFFFFF), GGGGGGGGGGGG, ((HHHHHHHHHHH<<2)|(IIIIIIII<<1))}

class StructInit : public Module
{
public:
    static constexpr unsigned AAAAAAAA = 17;
    static constexpr unsigned AAAAAAA1 = 65;
    static constexpr unsigned BBBBBBBB = 9;
    static constexpr unsigned CCCCCCCCC = 6;
    static constexpr unsigned DDDDDDDDD = 44;
    static constexpr unsigned EEEEE = 20;
    static constexpr unsigned FFFFFFFFFFFFFFFF = 32;
    static constexpr unsigned GGGGGGGGGGGG = 2;
    static constexpr unsigned HHHHHHHHHHH = 1;
    static constexpr unsigned IIIIIIII = 1;

    _PORT(UUU) value_out = _ASSIGN_COMB(value_comb_func());

private:
    UUU value_comb;

    UUU& value_comb_func()
    {
        auto uuu = FILL_UUU;
        value_comb = uuu;
        return value_comb;
    }

public:
    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

#undef log2

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include <vector>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static bool check_value(const UUU& value)
{
    return value.aaaaaaaa == clog2(StructInit::AAAAAAAA) &&
        value.aaaaaaa1 == clog2(StructInit::AAAAAAA1) - 5 &&
        value.bbbbbbbb == StructInit::BBBBBBBB &&
        value.cccccccc == StructInit::CCCCCCCCC &&
        value.ddddddddd == (StructInit::DDDDDDDDD >> 2) &&
        value.eeeeeeeee == (StructInit::EEEEE >> 2) &&
        value.FFFFFFFFFFFFFFFF == clog2(StructInit::FFFFFFFFFFFFFFFF) &&
        value.gggggggggg == StructInit::GGGGGGGGGGGG &&
        value.hhhhhhhhhhh == ((StructInit::HHHHHHHHHHH << 2) | (StructInit::IIIIIIII << 1));
}

int main()
{
    bool ok = true;
#ifdef VERILATOR
    VERILATOR_MODEL dut;
    UUU value{};
    dut.clk = 0;
    dut.reset = 0;
    dut.eval();
    std::memcpy(&value, &dut.value_out, sizeof(value));
    ok = check_value(value);
#else
    StructInit dut;
    dut._assign();
    ok = check_value(dut.value_out());
    if (ok) {
        ok &= VerilatorCompile(__FILE__, "StructInit", {"UUU_pkg"}, {"../../../../include"});
    }
    if (ok) {
        ok &= SystemEcho("StructInit/obj_dir/VStructInit") == 0;
    }
#endif
    if (!ok) {
        std::print("ERROR: aggregate bit-field struct initialization failed\n");
    }
    return ok ? 0 : 1;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
