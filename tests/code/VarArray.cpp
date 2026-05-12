#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

struct PackedVarItem
{
    unsigned lo:4;
    unsigned hi:4;
} __PACKED;

class VarArray : public Module
{
public:
    _PORT(u<16>) seed_in;
    _PORT(u<16>) comb_out = _ASSIGN_REG(comb_comb_func());
    _PORT(logic<16>) logic_out = _ASSIGN_REG(logic_comb_func());
    _PORT(u<16>) reg_out = _ASSIGN_REG(reg_comb_func());

private:
    u<16> c_1d[3];
    u<16> c_2d[2][3];
    u<16> c_3d[2][2][2];

    logic<16> logic_1d[2];
    logic<16> logic_2d[2][2];
    logic<16> logic_3d[2][2][2];

    array<u<16>, 3> cpp_1d;
    array<array<u<16>, 3>, 2> cpp_2d;
    array<array<array<u<16>, 2>, 2>, 2> cpp_3d;
    array<u<16>, 2> mixed_c_cpp[2][2];
    array<PackedVarItem, 2> cpp_struct_1d;
    array<u<16>, 4> cpp_type_1d;
    array<array<PackedVarItem, 2>, 3> cpp_struct_2d;
    array<array<u<16>, 3>, 2> cpp_type_2d;

    reg<u<16>> reg_1d[2];
    reg<u<16>> reg_2d[2][2];
    reg<u<16>> reg_3d[2][2][2];
    reg<array<u<16>, 2>> reg_cpp[2];
    reg<array<u<16>, 2>> reg_mixed[2][2];

    u<16> comb_comb;
    logic<16> logic_comb;
    u<16> reg_comb;

    u<16>& comb_comb_func()
    {
        c_1d[0] = seed_in() + u<16>(1);
        c_1d[1] = c_1d[0] + u<16>(2);
        c_1d[2] = c_1d[1] + u<16>(3);

        c_2d[0][0] = c_1d[2] + u<16>(10);
        c_2d[0][1] = c_2d[0][0] + u<16>(11);
        c_2d[0][2] = c_2d[0][1] + u<16>(12);
        c_2d[1][0] = c_1d[2] + u<16>(20);
        c_2d[1][1] = c_2d[1][0] + u<16>(21);
        c_2d[1][2] = c_2d[1][1] + u<16>(22);

        c_3d[0][0][0] = c_2d[0][1] + u<16>(100);
        c_3d[0][0][1] = c_3d[0][0][0] + u<16>(101);
        c_3d[0][1][0] = c_2d[0][2] + u<16>(110);
        c_3d[0][1][1] = c_3d[0][1][0] + u<16>(111);
        c_3d[1][0][0] = c_2d[1][1] + u<16>(200);
        c_3d[1][0][1] = c_3d[1][0][0] + u<16>(201);
        c_3d[1][1][0] = c_2d[1][2] + u<16>(210);
        c_3d[1][1][1] = c_3d[1][1][0] + u<16>(211);

        cpp_1d[0] = c_1d[2] + u<16>(30);
        cpp_1d[1] = cpp_1d[0] + u<16>(31);
        cpp_1d[2] = cpp_1d[1] + u<16>(32);

        cpp_2d[0][0] = cpp_1d[2] + u<16>(40);
        cpp_2d[0][1] = cpp_2d[0][0] + u<16>(41);
        cpp_2d[0][2] = cpp_2d[0][1] + u<16>(42);
        cpp_2d[1][0] = cpp_1d[2] + u<16>(50);
        cpp_2d[1][1] = cpp_2d[1][0] + u<16>(51);
        cpp_2d[1][2] = cpp_2d[1][1] + u<16>(52);

        cpp_3d[0][0][0] = cpp_2d[0][1] + u<16>(60);
        cpp_3d[0][0][1] = cpp_3d[0][0][0] + u<16>(61);
        cpp_3d[0][1][0] = cpp_2d[0][2] + u<16>(62);
        cpp_3d[0][1][1] = cpp_3d[0][1][0] + u<16>(63);
        cpp_3d[1][0][0] = cpp_2d[1][1] + u<16>(64);
        cpp_3d[1][0][1] = cpp_3d[1][0][0] + u<16>(65);
        cpp_3d[1][1][0] = cpp_2d[1][2] + u<16>(66);
        cpp_3d[1][1][1] = cpp_3d[1][1][0] + u<16>(67);

        mixed_c_cpp[0][0][0] = cpp_3d[0][1][1] + u<16>(70);
        mixed_c_cpp[0][0][1] = mixed_c_cpp[0][0][0] + u<16>(71);
        mixed_c_cpp[0][1][0] = cpp_3d[1][0][1] + u<16>(72);
        mixed_c_cpp[0][1][1] = mixed_c_cpp[0][1][0] + u<16>(73);
        mixed_c_cpp[1][0][0] = cpp_3d[1][1][0] + u<16>(74);
        mixed_c_cpp[1][0][1] = mixed_c_cpp[1][0][0] + u<16>(75);
        mixed_c_cpp[1][1][0] = cpp_3d[1][1][1] + u<16>(76);
        mixed_c_cpp[1][1][1] = mixed_c_cpp[1][1][0] + u<16>(77);

        cpp_struct_1d[0].lo = static_cast<unsigned>(mixed_c_cpp[1][1][1]) & 0xf;
        cpp_struct_1d[0].hi = (static_cast<unsigned>(mixed_c_cpp[1][1][1]) >> 4) & 0xf;
        cpp_struct_1d[1].lo = (cpp_struct_1d[0].lo + 1) & 0xf;
        cpp_struct_1d[1].hi = (cpp_struct_1d[0].hi + 2) & 0xf;
        cpp_type_1d[0] = mixed_c_cpp[1][1][1] + u<16>(80);
        cpp_type_1d[1] = cpp_type_1d[0] + u<16>(81);
        cpp_type_1d[2] = cpp_type_1d[1] + u<16>(82);
        cpp_type_1d[3] = cpp_type_1d[2] + u<16>(83);
        cpp_struct_2d[0][0] = cpp_struct_1d[0];
        cpp_struct_2d[0][1] = cpp_struct_1d[1];
        cpp_struct_2d[1][0].lo = (cpp_struct_2d[0][1].lo + 3) & 0xf;
        cpp_struct_2d[1][0].hi = (cpp_struct_2d[0][1].hi + 4) & 0xf;
        cpp_struct_2d[1][1].lo = (cpp_struct_2d[1][0].lo + 5) & 0xf;
        cpp_struct_2d[1][1].hi = (cpp_struct_2d[1][0].hi + 6) & 0xf;
        cpp_struct_2d[2][0].lo = (cpp_struct_2d[1][1].lo + 7) & 0xf;
        cpp_struct_2d[2][0].hi = (cpp_struct_2d[1][1].hi + 8) & 0xf;
        cpp_struct_2d[2][1].lo = (cpp_struct_2d[2][0].lo + 9) & 0xf;
        cpp_struct_2d[2][1].hi = (cpp_struct_2d[2][0].hi + 10) & 0xf;
        cpp_type_2d[0][0] = cpp_type_1d[3] + u<16>(84);
        cpp_type_2d[0][1] = cpp_type_2d[0][0] + u<16>(85);
        cpp_type_2d[0][2] = cpp_type_2d[0][1] + u<16>(86);
        cpp_type_2d[1][0] = cpp_type_2d[0][2] + u<16>(87);
        cpp_type_2d[1][1] = cpp_type_2d[1][0] + u<16>(88);
        cpp_type_2d[1][2] = cpp_type_2d[1][1] + u<16>(89);

        return comb_comb = c_3d[1][1][1] + cpp_3d[1][1][1] + mixed_c_cpp[1][1][1]
            + u<16>(cpp_struct_2d[2][1].lo) + u<16>(cpp_struct_2d[2][1].hi) + cpp_type_2d[1][2];
    }

    logic<16>& logic_comb_func()
    {
        logic_1d[0] = logic<16>(seed_in() + u<16>(300));
        logic_1d[1] = logic_1d[0] + logic<16>(301);
        logic_2d[0][0] = logic_1d[1] + logic<16>(302);
        logic_2d[0][1] = logic_2d[0][0] + logic<16>(303);
        logic_2d[1][0] = logic_2d[0][1] + logic<16>(304);
        logic_2d[1][1] = logic_2d[1][0] + logic<16>(305);
        logic_3d[0][0][0] = logic_2d[1][1] + logic<16>(306);
        logic_3d[0][0][1] = logic_3d[0][0][0] + logic<16>(307);
        logic_3d[0][1][0] = logic_3d[0][0][1] + logic<16>(308);
        logic_3d[0][1][1] = logic_3d[0][1][0] + logic<16>(309);
        logic_3d[1][0][0] = logic_3d[0][1][1] + logic<16>(310);
        logic_3d[1][0][1] = logic_3d[1][0][0] + logic<16>(311);
        logic_3d[1][1][0] = logic_3d[1][0][1] + logic<16>(312);
        logic_3d[1][1][1] = logic_3d[1][1][0] + logic<16>(313);
        return logic_comb = logic_3d[1][1][1];
    }

    u<16>& reg_comb_func()
    {
        return reg_comb = reg_1d[1] + reg_2d[1][1] + reg_3d[1][1][1] + reg_cpp[1][1] + reg_mixed[1][1][1];
    }

public:
    void _work(bool reset)
    {
        if (reset) {
            reg_1d[0]._next = 0;
            reg_1d[1]._next = 0;
            reg_2d[0][0]._next = 0;
            reg_2d[0][1]._next = 0;
            reg_2d[1][0]._next = 0;
            reg_2d[1][1]._next = 0;
            reg_3d[0][0][0]._next = 0;
            reg_3d[0][0][1]._next = 0;
            reg_3d[0][1][0]._next = 0;
            reg_3d[0][1][1]._next = 0;
            reg_3d[1][0][0]._next = 0;
            reg_3d[1][0][1]._next = 0;
            reg_3d[1][1][0]._next = 0;
            reg_3d[1][1][1]._next = 0;
            reg_cpp[0]._next[0] = 0;
            reg_cpp[0]._next[1] = 0;
            reg_cpp[1]._next[0] = 0;
            reg_cpp[1]._next[1] = 0;
            reg_mixed[0][0]._next[0] = 0;
            reg_mixed[0][0]._next[1] = 0;
            reg_mixed[0][1]._next[0] = 0;
            reg_mixed[0][1]._next[1] = 0;
            reg_mixed[1][0]._next[0] = 0;
            reg_mixed[1][0]._next[1] = 0;
            reg_mixed[1][1]._next[0] = 0;
            reg_mixed[1][1]._next[1] = 0;
            return;
        }

        reg_1d[0]._next = seed_in() + u<16>(5);
        reg_1d[1]._next = reg_1d[0] + u<16>(6);
        reg_2d[0][0]._next = reg_1d[1] + u<16>(7);
        reg_2d[0][1]._next = reg_2d[0][0] + u<16>(8);
        reg_2d[1][0]._next = reg_2d[0][1] + u<16>(9);
        reg_2d[1][1]._next = reg_2d[1][0] + u<16>(10);
        reg_3d[0][0][0]._next = reg_2d[1][1] + u<16>(11);
        reg_3d[0][0][1]._next = reg_3d[0][0][0] + u<16>(12);
        reg_3d[0][1][0]._next = reg_3d[0][0][1] + u<16>(13);
        reg_3d[0][1][1]._next = reg_3d[0][1][0] + u<16>(14);
        reg_3d[1][0][0]._next = reg_3d[0][1][1] + u<16>(15);
        reg_3d[1][0][1]._next = reg_3d[1][0][0] + u<16>(16);
        reg_3d[1][1][0]._next = reg_3d[1][0][1] + u<16>(17);
        reg_3d[1][1][1]._next = reg_3d[1][1][0] + u<16>(18);
        reg_cpp[0]._next[0] = reg_3d[1][1][1] + u<16>(19);
        reg_cpp[0]._next[1] = reg_cpp[0][0] + u<16>(20);
        reg_cpp[1]._next[0] = reg_cpp[0][1] + u<16>(21);
        reg_cpp[1]._next[1] = reg_cpp[1][0] + u<16>(22);
        reg_mixed[0][0]._next[0] = reg_cpp[1][1] + u<16>(23);
        reg_mixed[0][0]._next[1] = reg_mixed[0][0][0] + u<16>(24);
        reg_mixed[0][1]._next[0] = reg_mixed[0][0][1] + u<16>(25);
        reg_mixed[0][1]._next[1] = reg_mixed[0][1][0] + u<16>(26);
        reg_mixed[1][0]._next[0] = reg_mixed[0][1][1] + u<16>(27);
        reg_mixed[1][0]._next[1] = reg_mixed[1][0][0] + u<16>(28);
        reg_mixed[1][1]._next[0] = reg_mixed[1][0][1] + u<16>(29);
        reg_mixed[1][1]._next[1] = reg_mixed[1][1][0] + u<16>(30);
    }

    void _strobe()
    {
        reg_1d[0].strobe();
        reg_1d[1].strobe();
        reg_2d[0][0].strobe();
        reg_2d[0][1].strobe();
        reg_2d[1][0].strobe();
        reg_2d[1][1].strobe();
        reg_3d[0][0][0].strobe();
        reg_3d[0][0][1].strobe();
        reg_3d[0][1][0].strobe();
        reg_3d[0][1][1].strobe();
        reg_3d[1][0][0].strobe();
        reg_3d[1][0][1].strobe();
        reg_3d[1][1][0].strobe();
        reg_3d[1][1][1].strobe();
        reg_cpp[0].strobe();
        reg_cpp[1].strobe();
        reg_mixed[0][0].strobe();
        reg_mixed[0][1].strobe();
        reg_mixed[1][0].strobe();
        reg_mixed[1][1].strobe();
    }

    void _assign() {}
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

static uint16_t expected_comb(uint16_t seed)
{
    uint16_t c1[3] = {};
    uint16_t c2[2][3] = {};
    uint16_t c3[2][2][2] = {};
    uint16_t a1[3] = {};
    uint16_t a2[2][3] = {};
    uint16_t a3[2][2][2] = {};
    uint16_t mixed[2][2][2] = {};
    uint8_t s1_lo[2] = {};
    uint8_t s1_hi[2] = {};
    uint16_t t1[4] = {};
    uint8_t s2_lo[3][2] = {};
    uint8_t s2_hi[3][2] = {};
    uint16_t t2[2][3] = {};

    c1[0] = seed + 1;
    c1[1] = c1[0] + 2;
    c1[2] = c1[1] + 3;
    c2[0][0] = c1[2] + 10;
    c2[0][1] = c2[0][0] + 11;
    c2[0][2] = c2[0][1] + 12;
    c2[1][0] = c1[2] + 20;
    c2[1][1] = c2[1][0] + 21;
    c2[1][2] = c2[1][1] + 22;
    c3[0][0][0] = c2[0][1] + 100;
    c3[0][0][1] = c3[0][0][0] + 101;
    c3[0][1][0] = c2[0][2] + 110;
    c3[0][1][1] = c3[0][1][0] + 111;
    c3[1][0][0] = c2[1][1] + 200;
    c3[1][0][1] = c3[1][0][0] + 201;
    c3[1][1][0] = c2[1][2] + 210;
    c3[1][1][1] = c3[1][1][0] + 211;

    a1[0] = c1[2] + 30;
    a1[1] = a1[0] + 31;
    a1[2] = a1[1] + 32;
    a2[0][0] = a1[2] + 40;
    a2[0][1] = a2[0][0] + 41;
    a2[0][2] = a2[0][1] + 42;
    a2[1][0] = a1[2] + 50;
    a2[1][1] = a2[1][0] + 51;
    a2[1][2] = a2[1][1] + 52;
    a3[0][0][0] = a2[0][1] + 60;
    a3[0][0][1] = a3[0][0][0] + 61;
    a3[0][1][0] = a2[0][2] + 62;
    a3[0][1][1] = a3[0][1][0] + 63;
    a3[1][0][0] = a2[1][1] + 64;
    a3[1][0][1] = a3[1][0][0] + 65;
    a3[1][1][0] = a2[1][2] + 66;
    a3[1][1][1] = a3[1][1][0] + 67;

    mixed[0][0][0] = a3[0][1][1] + 70;
    mixed[0][0][1] = mixed[0][0][0] + 71;
    mixed[0][1][0] = a3[1][0][1] + 72;
    mixed[0][1][1] = mixed[0][1][0] + 73;
    mixed[1][0][0] = a3[1][1][0] + 74;
    mixed[1][0][1] = mixed[1][0][0] + 75;
    mixed[1][1][0] = a3[1][1][1] + 76;
    mixed[1][1][1] = mixed[1][1][0] + 77;

    s1_lo[0] = mixed[1][1][1] & 0xf;
    s1_hi[0] = (mixed[1][1][1] >> 4) & 0xf;
    s1_lo[1] = (s1_lo[0] + 1) & 0xf;
    s1_hi[1] = (s1_hi[0] + 2) & 0xf;
    t1[0] = mixed[1][1][1] + 80;
    t1[1] = t1[0] + 81;
    t1[2] = t1[1] + 82;
    t1[3] = t1[2] + 83;
    s2_lo[0][0] = s1_lo[0];
    s2_hi[0][0] = s1_hi[0];
    s2_lo[0][1] = s1_lo[1];
    s2_hi[0][1] = s1_hi[1];
    s2_lo[1][0] = (s2_lo[0][1] + 3) & 0xf;
    s2_hi[1][0] = (s2_hi[0][1] + 4) & 0xf;
    s2_lo[1][1] = (s2_lo[1][0] + 5) & 0xf;
    s2_hi[1][1] = (s2_hi[1][0] + 6) & 0xf;
    s2_lo[2][0] = (s2_lo[1][1] + 7) & 0xf;
    s2_hi[2][0] = (s2_hi[1][1] + 8) & 0xf;
    s2_lo[2][1] = (s2_lo[2][0] + 9) & 0xf;
    s2_hi[2][1] = (s2_hi[2][0] + 10) & 0xf;
    t2[0][0] = t1[3] + 84;
    t2[0][1] = t2[0][0] + 85;
    t2[0][2] = t2[0][1] + 86;
    t2[1][0] = t2[0][2] + 87;
    t2[1][1] = t2[1][0] + 88;
    t2[1][2] = t2[1][1] + 89;

    return c3[1][1][1] + a3[1][1][1] + mixed[1][1][1] + s2_lo[2][1] + s2_hi[2][1] + t2[1][2];
}

static logic<16> expected_logic(uint16_t seed)
{
    uint16_t value = seed;
    for (uint16_t add = 300; add <= 313; ++add) {
        value = uint16_t(value + add);
    }
    return logic<16>(value);
}

struct RegState
{
    uint16_t r1[2] = {};
    uint16_t r2[2][2] = {};
    uint16_t r3[2][2][2] = {};
    uint16_t rc[2][2] = {};
    uint16_t rm[2][2][2] = {};
};

static uint16_t expected_reg_sum(const RegState& s)
{
    return s.r1[1] + s.r2[1][1] + s.r3[1][1][1] + s.rc[1][1] + s.rm[1][1][1];
}

static void step_reg_ref(RegState& s, uint16_t seed)
{
    RegState n = s;
    n.r1[0] = seed + 5;
    n.r1[1] = s.r1[0] + 6;
    n.r2[0][0] = s.r1[1] + 7;
    n.r2[0][1] = s.r2[0][0] + 8;
    n.r2[1][0] = s.r2[0][1] + 9;
    n.r2[1][1] = s.r2[1][0] + 10;
    n.r3[0][0][0] = s.r2[1][1] + 11;
    n.r3[0][0][1] = s.r3[0][0][0] + 12;
    n.r3[0][1][0] = s.r3[0][0][1] + 13;
    n.r3[0][1][1] = s.r3[0][1][0] + 14;
    n.r3[1][0][0] = s.r3[0][1][1] + 15;
    n.r3[1][0][1] = s.r3[1][0][0] + 16;
    n.r3[1][1][0] = s.r3[1][0][1] + 17;
    n.r3[1][1][1] = s.r3[1][1][0] + 18;
    n.rc[0][0] = s.r3[1][1][1] + 19;
    n.rc[0][1] = s.rc[0][0] + 20;
    n.rc[1][0] = s.rc[0][1] + 21;
    n.rc[1][1] = s.rc[1][0] + 22;
    n.rm[0][0][0] = s.rc[1][1] + 23;
    n.rm[0][0][1] = s.rm[0][0][0] + 24;
    n.rm[0][1][0] = s.rm[0][0][1] + 25;
    n.rm[0][1][1] = s.rm[0][1][0] + 26;
    n.rm[1][0][0] = s.rm[0][1][1] + 27;
    n.rm[1][0][1] = s.rm[1][0][0] + 28;
    n.rm[1][1][0] = s.rm[1][0][1] + 29;
    n.rm[1][1][1] = s.rm[1][1][0] + 30;
    s = n;
}

static bool generated_sv_has_var_arrays()
{
#ifdef VERILATOR
    const std::filesystem::path sv_path = "VarArray_1/VarArray.sv";
#else
    const std::filesystem::path sv_path = "generated/VarArray.sv";
#endif
    std::ifstream in(sv_path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", sv_path.string());
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool has_c_1d = text.find("c_1d[3]") != std::string::npos;
    bool has_c_2d = text.find("c_2d[2][3]") != std::string::npos;
    bool has_c_3d = text.find("c_3d[2][2][2]") != std::string::npos;
    bool has_logic_3d = text.find("logic_3d[2][2][2]") != std::string::npos;
    bool has_cpp_1d = text.find("logic[3-1:0][16-1:0] cpp_1d;") != std::string::npos;
    bool has_cpp_2d = text.find("logic[2-1:0][3-1:0][16-1:0] cpp_2d;") != std::string::npos;
    bool has_cpp_3d = text.find("logic[2-1:0][2-1:0][2-1:0][16-1:0] cpp_3d;") != std::string::npos;
    bool has_mixed = text.find("logic[2-1:0][16-1:0] mixed_c_cpp[2][2]") != std::string::npos;
    bool has_cpp_struct_1d = text.find("PackedVarItem[2-1:0] cpp_struct_1d;") != std::string::npos;
    bool has_cpp_type_1d = text.find("logic[4-1:0][16-1:0] cpp_type_1d;") != std::string::npos;
    bool has_cpp_struct_2d = text.find("PackedVarItem[3-1:0][2-1:0] cpp_struct_2d;") != std::string::npos;
    bool has_cpp_type_2d = text.find("logic[2-1:0][3-1:0][16-1:0] cpp_type_2d;") != std::string::npos;
    bool has_reg = text.find("reg_3d[2][2][2]") != std::string::npos;
    bool has_reg_mixed = text.find("reg_mixed[2][2]") != std::string::npos;

    if (!has_c_1d || !has_c_2d || !has_c_3d || !has_logic_3d || !has_cpp_1d || !has_cpp_2d
        || !has_cpp_3d || !has_mixed || !has_cpp_struct_1d || !has_cpp_type_1d || !has_cpp_struct_2d || !has_cpp_type_2d
        || !has_reg || !has_reg_mixed) {
        std::print("\nERROR: generated SV var arrays are incomplete: c1={}, c2={}, c3={}, logic3={}, cpp1={}, cpp2={}, cpp3={}, mixed={}, cpp_struct1={}, cpp_type1={}, cpp_struct2={}, cpp_type2={}, reg={}, reg_mixed={}\n",
            has_c_1d, has_c_2d, has_c_3d, has_logic_3d, has_cpp_1d, has_cpp_2d, has_cpp_3d, has_mixed,
            has_cpp_struct_1d, has_cpp_type_1d, has_cpp_struct_2d, has_cpp_type_2d, has_reg, has_reg_mixed);
        return false;
    }
    return true;
}

class TestVarArray : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    VarArray dut;
#endif

    u<16> seed;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = _ASSIGN_REG(seed);
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval_pos(bool reset)
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

    void eval_neg(bool reset)
    {
#ifdef VERILATOR
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
#else
        (void)reset;
#endif
    }

    void _strobe()
    {
#ifndef VERILATOR
        dut._strobe();
#endif
    }

    uint16_t comb()
    {
#ifdef VERILATOR
        return dut.comb_out;
#else
        return static_cast<uint16_t>(dut.comb_out());
#endif
    }

    logic<16> logic_result()
    {
#ifdef VERILATOR
        return logic<16>(dut.logic_out);
#else
        return dut.logic_out();
#endif
    }

    uint16_t reg_sum()
    {
#ifdef VERILATOR
        return dut.reg_out;
#else
        return static_cast<uint16_t>(dut.reg_out());
#endif
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestVarArray...");
#else
        std::print("CppHDL TestVarArray...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "var_array_test";
        _assign();

        error |= !generated_sv_has_var_arrays();

        RegState ref;
        seed = 0;
        eval_pos(true);
        _strobe();
        eval_neg(true);
        ++sys_clock;

        for (uint32_t i = 0; i < 32 && !error; ++i) {
            seed = u<16>(i * 13 + 9);
            uint16_t comb_ref = expected_comb(static_cast<uint16_t>(seed));
            logic<16> logic_ref = expected_logic(static_cast<uint16_t>(seed));

            eval_pos(false);
            uint16_t comb_got = comb();
            if (comb_got != comb_ref) {
                std::print("\ncomb ERROR at {}: got {}, expected {}\n", i, comb_got, comb_ref);
                error = true;
                break;
            }
            logic<16> logic_got = logic_result();
            if (logic_got != logic_ref) {
                std::print("\nlogic ERROR at {}: got {}, expected {}\n", i, logic_got, logic_ref);
                error = true;
                break;
            }

            step_reg_ref(ref, static_cast<uint16_t>(seed));
            _strobe();
            eval_neg(false);
            ++sys_clock;

            uint16_t reg_got = reg_sum();
            uint16_t reg_ref = expected_reg_sum(ref);
            if (reg_got != reg_ref) {
                std::print("\nreg ERROR at {}: got {}, expected {}\n", i, reg_got, reg_ref);
                error = true;
                break;
            }
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
        ok &= VerilatorCompile(__FILE__, "VarArray", {"Predef_pkg", "PackedVarItem_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("VarArray_1/obj_dir/VVarArray") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestVarArray().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
