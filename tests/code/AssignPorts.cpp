#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

class AssignPortsLeaf : public Module
{
public:
    __PORT(u<16>) value_in;
    __PORT(u<16>) value_out = __VAR(value_comb_func());

private:
    u<16> value_comb;

    u<16>& value_comb_func()
    {
        return value_comb = value_in() * u<16>(3) + u<16>(5);
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

class AssignPorts : public Module
{
public:
    __PORT(u<16>) seed_in;
    __PORT(u<16>) result_out = __VAR(result_comb_func());

private:
    AssignPortsLeaf expr_leaf;
    AssignPortsLeaf var_leaf;
    AssignPortsLeaf expr_i[3];
    AssignPortsLeaf var_i[3];
    AssignPortsLeaf expr_j[3];
    AssignPortsLeaf var_j[3];
    AssignPortsLeaf expr_ij[2][3];
    AssignPortsLeaf var_ij[2][3];
    AssignPortsLeaf expr_ijk[2][2][2];
    AssignPortsLeaf var_ijk[2][2][2];
    AssignPortsLeaf expr_cap[2][2][2];
    AssignPortsLeaf var_cap[2][2][2];

    u<16> scalar_source;
    u<16> i_source[3];
    u<16> j_source[3];
    u<16> ij_source[2][3];
    u<16> ijk_source[2][2][2];
    u<16> cap_source[2][2][2];

    u<16> result_comb;

    u<16>& result_comb_func()
    {
        scalar_source = seed_in() + u<16>(2);
        i_source[0] = seed_in() + u<16>(100);
        i_source[1] = seed_in() + u<16>(101);
        i_source[2] = seed_in() + u<16>(102);
        j_source[0] = seed_in() + u<16>(140);
        j_source[1] = seed_in() + u<16>(141);
        j_source[2] = seed_in() + u<16>(142);
        ij_source[0][0] = seed_in() + u<16>(200);
        ij_source[0][1] = seed_in() + u<16>(201);
        ij_source[0][2] = seed_in() + u<16>(202);
        ij_source[1][0] = seed_in() + u<16>(210);
        ij_source[1][1] = seed_in() + u<16>(211);
        ij_source[1][2] = seed_in() + u<16>(212);
        ijk_source[0][0][0] = seed_in() + u<16>(300);
        ijk_source[0][0][1] = seed_in() + u<16>(301);
        ijk_source[0][1][0] = seed_in() + u<16>(310);
        ijk_source[0][1][1] = seed_in() + u<16>(311);
        ijk_source[1][0][0] = seed_in() + u<16>(400);
        ijk_source[1][0][1] = seed_in() + u<16>(401);
        ijk_source[1][1][0] = seed_in() + u<16>(410);
        ijk_source[1][1][1] = seed_in() + u<16>(411);
        cap_source[0][0][0] = seed_in() + u<16>(500);
        cap_source[0][0][1] = seed_in() + u<16>(501);
        cap_source[0][1][0] = seed_in() + u<16>(510);
        cap_source[0][1][1] = seed_in() + u<16>(511);
        cap_source[1][0][0] = seed_in() + u<16>(600);
        cap_source[1][0][1] = seed_in() + u<16>(601);
        cap_source[1][1][0] = seed_in() + u<16>(610);
        cap_source[1][1][1] = seed_in() + u<16>(611);

        result_comb = expr_leaf.value_out() + var_leaf.value_out();
        result_comb += expr_i[0].value_out() + var_i[0].value_out();
        result_comb += expr_i[1].value_out() + var_i[1].value_out();
        result_comb += expr_i[2].value_out() + var_i[2].value_out();
        result_comb += expr_j[0].value_out() + var_j[0].value_out();
        result_comb += expr_j[1].value_out() + var_j[1].value_out();
        result_comb += expr_j[2].value_out() + var_j[2].value_out();
        result_comb += expr_ij[0][0].value_out() + var_ij[0][0].value_out();
        result_comb += expr_ij[0][1].value_out() + var_ij[0][1].value_out();
        result_comb += expr_ij[0][2].value_out() + var_ij[0][2].value_out();
        result_comb += expr_ij[1][0].value_out() + var_ij[1][0].value_out();
        result_comb += expr_ij[1][1].value_out() + var_ij[1][1].value_out();
        result_comb += expr_ij[1][2].value_out() + var_ij[1][2].value_out();
        result_comb += expr_ijk[0][0][0].value_out() + var_ijk[0][0][0].value_out();
        result_comb += expr_ijk[0][0][1].value_out() + var_ijk[0][0][1].value_out();
        result_comb += expr_ijk[0][1][0].value_out() + var_ijk[0][1][0].value_out();
        result_comb += expr_ijk[0][1][1].value_out() + var_ijk[0][1][1].value_out();
        result_comb += expr_ijk[1][0][0].value_out() + var_ijk[1][0][0].value_out();
        result_comb += expr_ijk[1][0][1].value_out() + var_ijk[1][0][1].value_out();
        result_comb += expr_ijk[1][1][0].value_out() + var_ijk[1][1][0].value_out();
        result_comb += expr_ijk[1][1][1].value_out() + var_ijk[1][1][1].value_out();
        result_comb += expr_cap[0][0][0].value_out() + var_cap[0][0][0].value_out();
        result_comb += expr_cap[0][0][1].value_out() + var_cap[0][0][1].value_out();
        result_comb += expr_cap[0][1][0].value_out() + var_cap[0][1][0].value_out();
        result_comb += expr_cap[0][1][1].value_out() + var_cap[0][1][1].value_out();
        result_comb += expr_cap[1][0][0].value_out() + var_cap[1][0][0].value_out();
        result_comb += expr_cap[1][0][1].value_out() + var_cap[1][0][1].value_out();
        result_comb += expr_cap[1][1][0].value_out() + var_cap[1][1][0].value_out();
        result_comb += expr_cap[1][1][1].value_out() + var_cap[1][1][1].value_out();
        return result_comb;
    }

public:
    void _work(bool reset) {}
    void _strobe() {}

    void _assign()
    {
        expr_leaf.value_in = __EXPR(seed_in() + u<16>(1));
        expr_leaf._assign();
        var_leaf.value_in = __VAR(scalar_source);
        var_leaf._assign();

        for (size_t i = 0; i < 3; ++i) {
            expr_i[i].value_in = __EXPR_I(seed_in() + u<16>(10 + i));
            expr_i[i]._assign();
            var_i[i].value_in = __VAR_I(i_source[i]);
            var_i[i]._assign();
        }

        for (size_t j = 0; j < 3; ++j) {
            expr_j[j].value_in = __EXPR_J(seed_in() + u<16>(20 + j));
            expr_j[j]._assign();
            var_j[j].value_in = __VAR_J(j_source[j]);
            var_j[j]._assign();
        }

        for (size_t i = 0; i < 2; ++i) {
            for (size_t j = 0; j < 3; ++j) {
                expr_ij[i][j].value_in = __EXPR_IJ(seed_in() + u<16>(30 + i * 5 + j));
                expr_ij[i][j]._assign();
                var_ij[i][j].value_in = __VAR_IJ(ij_source[i][j]);
                var_ij[i][j]._assign();
            }
        }

        for (size_t i = 0; i < 2; ++i) {
            for (size_t j = 0; j < 2; ++j) {
                for (size_t k = 0; k < 2; ++k) {
                    expr_ijk[i][j][k].value_in = __EXPR_IJK(seed_in() + u<16>(60 + i * 10 + j * 3 + k));
                    expr_ijk[i][j][k]._assign();
                    var_ijk[i][j][k].value_in = __VAR_IJK(ijk_source[i][j][k]);
                    var_ijk[i][j][k]._assign();
                }
            }
        }

        for (size_t x = 0; x < 2; ++x) {
            for (size_t y = 0; y < 2; ++y) {
                for (size_t z = 0; z < 2; ++z) {
                    expr_cap[x][y][z].value_in = __EXPR_CAP((x, y, z), seed_in() + u<16>(90 + x * 10 + y * 3 + z));
                    expr_cap[x][y][z]._assign();
                    var_cap[x][y][z].value_in = __VAR_CAP((x, y, z), cap_source[x][y][z]);
                    var_cap[x][y][z]._assign();
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

static uint16_t leaf_ref(uint16_t value)
{
    return uint16_t(value * 3 + 5);
}

static uint16_t expected(uint16_t seed)
{
    uint16_t sum = leaf_ref(seed + 1) + leaf_ref(seed + 2);
    for (uint16_t i = 0; i < 3; ++i) {
        sum = uint16_t(sum + leaf_ref(seed + 10 + i));
        sum = uint16_t(sum + leaf_ref(seed + 100 + i));
    }
    for (uint16_t j = 0; j < 3; ++j) {
        sum = uint16_t(sum + leaf_ref(seed + 20 + j));
        sum = uint16_t(sum + leaf_ref(seed + 140 + j));
    }
    for (uint16_t i = 0; i < 2; ++i) {
        for (uint16_t j = 0; j < 3; ++j) {
            sum = uint16_t(sum + leaf_ref(seed + 30 + i * 5 + j));
            sum = uint16_t(sum + leaf_ref(seed + 200 + i * 10 + j));
        }
    }
    for (uint16_t i = 0; i < 2; ++i) {
        for (uint16_t j = 0; j < 2; ++j) {
            for (uint16_t k = 0; k < 2; ++k) {
                sum = uint16_t(sum + leaf_ref(seed + 60 + i * 10 + j * 3 + k));
                sum = uint16_t(sum + leaf_ref(seed + 300 + i * 100 + j * 10 + k));
            }
        }
    }
    for (uint16_t x = 0; x < 2; ++x) {
        for (uint16_t y = 0; y < 2; ++y) {
            for (uint16_t z = 0; z < 2; ++z) {
                sum = uint16_t(sum + leaf_ref(seed + 90 + x * 10 + y * 3 + z));
                sum = uint16_t(sum + leaf_ref(seed + 500 + x * 100 + y * 10 + z));
            }
        }
    }
    return sum;
}

static bool generated_sv_has_assign_port_arrays()
{
#ifdef VERILATOR
    const std::filesystem::path sv_path = "AssignPorts_1/AssignPorts.sv";
#else
    const std::filesystem::path sv_path = "generated/AssignPorts.sv";
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool has_i = text.find("expr_i__value_in[3]") != std::string::npos
        && text.find("var_i__value_in[3]") != std::string::npos;
    const bool has_j = text.find("expr_j__value_in[3]") != std::string::npos
        && text.find("var_j__value_in[3]") != std::string::npos;
    const bool has_ij = text.find("expr_ij__value_in[2][3]") != std::string::npos
        && text.find("var_ij__value_in[2][3]") != std::string::npos;
    const bool has_ijk = text.find("expr_ijk__value_in[2][2][2]") != std::string::npos
        && text.find("var_ijk__value_in[2][2][2]") != std::string::npos;
    const bool has_cap = text.find("expr_cap__value_in[2][2][2]") != std::string::npos
        && text.find("var_cap__value_in[2][2][2]") != std::string::npos;
    const bool has_cap_index = text.find(".value_in(expr_cap__value_in[gi][gj][gk])") != std::string::npos;

    if (!has_i || !has_j || !has_ij || !has_ijk || !has_cap || !has_cap_index) {
        std::print("\nERROR: generated SV assign-port arrays are incomplete: i={}, j={}, ij={}, ijk={}, cap={}, cap_index={}\n",
            has_i, has_j, has_ij, has_ijk, has_cap, has_cap_index);
        return false;
    }
    return true;
}

class TestAssignPorts : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    AssignPorts dut;
#endif

    u<16> seed;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = __VAR(seed);
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

    uint16_t result()
    {
#ifdef VERILATOR
        return dut.result_out;
#else
        return static_cast<uint16_t>(dut.result_out());
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestAssignPorts...");
#else
        std::print("CppHDL TestAssignPorts...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "assign_ports_test";
        _assign();

        error |= !generated_sv_has_assign_port_arrays();

        for (uint32_t i = 0; i < 128 && !error; ++i) {
            seed = u<16>(i * 11 + 7);
            eval(false);
            const uint16_t got = result();
            const uint16_t ref = expected(static_cast<uint16_t>(seed));
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
        ok &= VerilatorCompile(__FILE__, "AssignPorts", {"Predef_pkg", "AssignPortsLeaf"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("AssignPorts_1/obj_dir/VAssignPorts") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestAssignPorts().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
