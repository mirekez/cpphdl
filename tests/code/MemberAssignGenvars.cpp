#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

class MemberAssignGenvarsLeaf : public Module
{
public:
    _PORT(u<32>) value_in;
    _PORT(u<32>) value_out = _BIND_VAR(value_comb_func());

private:
    u<32> value_comb;

    u<32>& value_comb_func()
    {
        return value_comb = value_in() * u<32>(7) + u<32>(3);
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

class MemberAssignGenvars : public Module
{
public:
    _PORT(u<32>) seed_in;
    _PORT(u<32>) result_out = _BIND_VAR(result_comb_func());

private:
    MemberAssignGenvarsLeaf expr4[2][2][2][2];
    MemberAssignGenvarsLeaf var4[2][2][2][2];

    u<32> source5[2][2][2][2][2];
    u<32> result_comb;

    u<32>& result_comb_func()
    {
        source5[0][0][0][0][1] = seed_in() + u<32>(2001);
        source5[0][0][0][1][1] = seed_in() + u<32>(2002);
        source5[0][0][1][0][1] = seed_in() + u<32>(2003);
        source5[0][0][1][1][1] = seed_in() + u<32>(2004);
        source5[0][1][0][0][1] = seed_in() + u<32>(2011);
        source5[0][1][0][1][1] = seed_in() + u<32>(2012);
        source5[0][1][1][0][1] = seed_in() + u<32>(2013);
        source5[0][1][1][1][1] = seed_in() + u<32>(2014);
        source5[1][0][0][0][1] = seed_in() + u<32>(2101);
        source5[1][0][0][1][1] = seed_in() + u<32>(2102);
        source5[1][0][1][0][1] = seed_in() + u<32>(2103);
        source5[1][0][1][1][1] = seed_in() + u<32>(2104);
        source5[1][1][0][0][1] = seed_in() + u<32>(2111);
        source5[1][1][0][1][1] = seed_in() + u<32>(2112);
        source5[1][1][1][0][1] = seed_in() + u<32>(2113);
        source5[1][1][1][1][1] = seed_in() + u<32>(2114);

        result_comb = expr4[0][0][0][0].value_out() + var4[0][0][0][0].value_out();
        result_comb += expr4[0][0][0][1].value_out() + var4[0][0][0][1].value_out();
        result_comb += expr4[0][0][1][0].value_out() + var4[0][0][1][0].value_out();
        result_comb += expr4[0][0][1][1].value_out() + var4[0][0][1][1].value_out();
        result_comb += expr4[0][1][0][0].value_out() + var4[0][1][0][0].value_out();
        result_comb += expr4[0][1][0][1].value_out() + var4[0][1][0][1].value_out();
        result_comb += expr4[0][1][1][0].value_out() + var4[0][1][1][0].value_out();
        result_comb += expr4[0][1][1][1].value_out() + var4[0][1][1][1].value_out();
        result_comb += expr4[1][0][0][0].value_out() + var4[1][0][0][0].value_out();
        result_comb += expr4[1][0][0][1].value_out() + var4[1][0][0][1].value_out();
        result_comb += expr4[1][0][1][0].value_out() + var4[1][0][1][0].value_out();
        result_comb += expr4[1][0][1][1].value_out() + var4[1][0][1][1].value_out();
        result_comb += expr4[1][1][0][0].value_out() + var4[1][1][0][0].value_out();
        result_comb += expr4[1][1][0][1].value_out() + var4[1][1][0][1].value_out();
        result_comb += expr4[1][1][1][0].value_out() + var4[1][1][1][0].value_out();
        result_comb += expr4[1][1][1][1].value_out() + var4[1][1][1][1].value_out();
        return result_comb;
    }

public:
    void _work(bool reset) {}
    void _strobe() {}

    void _assign()
    {
        for (size_t a = 0; a < 2; ++a) {
            for (size_t b = 0; b < 2; ++b) {
                for (size_t c = 0; c < 2; ++c) {
                    for (size_t d = 0; d < 2; ++d) {
                        for (size_t e = 0; e < 2; ++e) {
                            if (e == 0) {
                                expr4[a][b][c][d].value_in = _BIND_CAP((a, b, c, d, e),
                                    seed_in() + u<32>(1000 + a * 100 + b * 10 + c * 2 + d + e));
                                expr4[a][b][c][d]._assign();
                            }
                            if (e == 1) {
                                var4[a][b][c][d].value_in = _BIND_VAR_CAP((a, b, c, d, e),
                                    source5[a][b][c][d][e]);
                                var4[a][b][c][d]._assign();
                            }
                        }
                    }
                }
            }
        }
    }
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

static uint32_t leaf_ref(uint32_t value)
{
    return value * 7 + 3;
}

static uint32_t expected(uint32_t seed)
{
    uint32_t sum = 0;
    for (uint32_t a = 0; a < 2; ++a) {
        for (uint32_t b = 0; b < 2; ++b) {
            for (uint32_t c = 0; c < 2; ++c) {
                for (uint32_t d = 0; d < 2; ++d) {
                    sum += leaf_ref(seed + 1000 + a * 100 + b * 10 + c * 2 + d);
                    sum += leaf_ref(seed + 2000 + a * 100 + b * 10 + c * 2 + d + 1);
                }
            }
        }
    }
    return sum;
}

static bool generated_sv_has_smart_genvars()
{
#ifdef VERILATOR
    const std::filesystem::path sv_path = "MemberAssignGenvars_1/MemberAssignGenvars.sv";
#else
    const std::filesystem::path sv_path = "generated/MemberAssignGenvars.sv";
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool has_member_genvars = text.find("genvar __i, __j, __k, __l;") != std::string::npos;
    const bool has_member_4d = text.find("expr4__value_in[2][2][2][2]") != std::string::npos
        && text.find(".value_in(expr4__value_in[__i][__j][__k][__l])") != std::string::npos;
    const bool has_assign_genvars = text.find("genvar ga;") != std::string::npos
        && text.find("genvar gb;") != std::string::npos
        && text.find("genvar gc;") != std::string::npos
        && text.find("genvar gd;") != std::string::npos
        && text.find("genvar ge;") != std::string::npos;
    const bool has_assign_5d = text.find("for (ge = 'h0;ge < 'h2;ge=ge+1)") != std::string::npos;
    const bool no_old_member_genvars = text.find("genvar gi, gj, gk;") == std::string::npos;

    if (!has_member_genvars || !has_member_4d || !has_assign_genvars || !has_assign_5d || !no_old_member_genvars) {
        std::print("\nERROR: generated SV genvars are wrong: member_genvars={}, member_4d={}, assign_genvars={}, assign_5d={}, no_old={}\n",
            has_member_genvars, has_member_4d, has_assign_genvars, has_assign_5d, no_old_member_genvars);
        return false;
    }
    return true;
}

class TestMemberAssignGenvars : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    MemberAssignGenvars dut;
#endif

    u<32> seed;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = _BIND_VAR(seed);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval(bool reset)
    {
#ifdef VERILATOR
        dut.seed_in = seed;
        dut.clk = 1;
        dut.reset = reset;
        dut.eval();
#else
        dut._work(reset);
#endif
    }

    void neg(bool reset)
    {
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
#else
        (void)reset;
#endif
    }

    uint32_t result()
    {
#ifdef VERILATOR
        return dut.result_out;
#else
        return static_cast<uint32_t>(dut.result_out());
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestMemberAssignGenvars...");
#else
        std::print("CppHDL TestMemberAssignGenvars...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "member_assign_genvars_test";
        _assign();

        error |= !generated_sv_has_smart_genvars();

        for (uint32_t i = 0; i < 64 && !error; ++i) {
            seed = u<32>(i * 19 + 11);
            eval(false);
            const uint32_t got = result();
            const uint32_t ref = expected(static_cast<uint32_t>(seed));
            if (got != ref) {
                std::print("\nresult ERROR at {}: got {}, expected {}\n", i, got, ref);
                error = true;
            }
            neg(false);
            ++sys_clock;
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "MemberAssignGenvars", {"Predef_pkg", "MemberAssignGenvarsLeaf"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("MemberAssignGenvars_1/obj_dir/VMemberAssignGenvars") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestMemberAssignGenvars().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
