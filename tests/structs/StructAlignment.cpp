#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

struct TinyBits
{
    unsigned a:1;
    unsigned b:2;
    unsigned c:3;
} __PACKED;

struct MixedBits
{
    unsigned flag:1;
    unsigned code:4;
    u<3> state;
    unsigned tail:2;
} __PACKED;

struct OuterBits
{
    unsigned head:3;
    TinyBits tiny;
    unsigned mid:5;
    MixedBits mixed;
    u<4> nibble;
    unsigned last:1;
} __PACKED;

union InnerUnion
{
    struct {
        unsigned ia:2;
        u<5> ib;
        unsigned ic:3;
    } __PACKED s;
    struct {
        unsigned id:4;
        unsigned ie:1;
        u<7> iff;
    } __PACKED t;
} __PACKED;

struct StructWithUnion
{
    unsigned prefix:3;
    InnerUnion inner;
    unsigned suffix:5;
} __PACKED;

union UnionContainingStructContainingUnion
{
    struct {
        unsigned ua:1;
        StructWithUnion nested;
        unsigned ub:6;
    } __PACKED wrapped;
    struct {
        unsigned alt0:7;
        u<9> alt1;
        unsigned alt2:2;
    } __PACKED alt;
} __PACKED;

struct UnionStruct
{
    unsigned sa:4;
    u<6> sb;
    unsigned sc:1;
} __PACKED;

union UnionWithStruct
{
    struct {
        unsigned us0:2;
        UnionStruct nested;
        unsigned us1:3;
    } __PACKED branch;
    struct {
        u<11> other0;
        unsigned other1:5;
    } __PACKED other;
} __PACKED;

namespace AlignMode
{
enum MODES
{
    MODE_ZERO,
    MODE_ONE,
    MODE_TWO,
    MODE_THREE
};
}

struct StructWithEnum
{
    unsigned prefix:2;
    AlignMode::MODES mode;
    TinyBits tiny;
    unsigned suffix:3;
} __PACKED;

struct StructContainingUnionContainingStruct
{
    unsigned head:2;
    UnionWithStruct u;
    unsigned tail:7;
} __PACKED;

class StructAlignment : public Module
{
public:
    _PORT(u<8>) seed_in;
    _PORT(OuterBits) sample_out = _ASSIGN_REG(sample_comb_func());
    _PORT(UnionContainingStructContainingUnion) union_struct_out = _ASSIGN_REG(union_struct_comb_func());
    _PORT(StructContainingUnionContainingStruct) struct_union_out = _ASSIGN_REG(struct_union_comb_func());
    _PORT(StructWithEnum) enum_struct_out = _ASSIGN_REG(enum_struct_comb_func());

private:
    OuterBits sample_comb;
    UnionContainingStructContainingUnion union_struct_comb;
    StructContainingUnionContainingStruct struct_union_comb;
    StructWithEnum enum_struct_comb;

    OuterBits& sample_comb_func()
    {
        uint32_t seed = seed_in();
        sample_comb = {};
        sample_comb.head = seed & 0x7;
        sample_comb.tiny.a = (seed >> 1) & 0x1;
        sample_comb.tiny.b = (seed >> 2) & 0x3;
        sample_comb.tiny.c = (seed >> 4) & 0x7;
        sample_comb.mid = (seed + 3) & 0x1f;
        sample_comb.mixed.flag = (seed >> 3) & 0x1;
        sample_comb.mixed.code = (seed + 5) & 0xf;
        sample_comb.mixed.state = u<3>(seed + 2);
        sample_comb.mixed.tail = (seed >> 5) & 0x3;
        sample_comb.nibble = u<4>(seed ^ 0xa);
        sample_comb.last = (seed >> 7) & 0x1;
        return sample_comb;
    }

    UnionContainingStructContainingUnion& union_struct_comb_func()
    {
        uint32_t seed = seed_in();
        union_struct_comb = {};
        union_struct_comb.wrapped.ua = seed & 0x1;
        union_struct_comb.wrapped.nested.prefix = (seed >> 1) & 0x7;
        union_struct_comb.wrapped.nested.inner.s.ia = (seed + 1) & 0x3;
        union_struct_comb.wrapped.nested.inner.s.ib = u<5>(seed + 3);
        union_struct_comb.wrapped.nested.inner.s.ic = (seed >> 2) & 0x7;
        union_struct_comb.wrapped.ub = (seed + 5) & 0x3f;
        return union_struct_comb;
    }

    StructContainingUnionContainingStruct& struct_union_comb_func()
    {
        uint32_t seed = seed_in();
        struct_union_comb = {};
        struct_union_comb.head = (seed >> 2) & 0x3;
        struct_union_comb.u.branch.us0 = seed & 0x3;
        struct_union_comb.u.branch.nested.sa = (seed + 7) & 0xf;
        struct_union_comb.u.branch.nested.sb = u<6>(seed + 11);
        struct_union_comb.u.branch.nested.sc = (seed >> 6) & 0x1;
        struct_union_comb.u.branch.us1 = (seed >> 3) & 0x7;
        struct_union_comb.tail = (seed + 13) & 0x7f;
        return struct_union_comb;
    }

    StructWithEnum& enum_struct_comb_func()
    {
        uint32_t seed = seed_in();
        enum_struct_comb = {};
        enum_struct_comb.prefix = seed & 0x3;
        enum_struct_comb.mode = (seed & 0x1) ? AlignMode::MODE_THREE : AlignMode::MODE_ONE;
        enum_struct_comb.tiny.a = (seed >> 1) & 0x1;
        enum_struct_comb.tiny.b = (seed >> 2) & 0x3;
        enum_struct_comb.tiny.c = (seed >> 4) & 0x7;
        enum_struct_comb.suffix = (seed + 3) & 0x7;
        return enum_struct_comb;
    }

public:
    void _work(bool reset) {}
    void _strobe() {}
    void _assign() {}
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

static OuterBits expected_sample(uint32_t seed)
{
    OuterBits sample{};
    sample.head = seed & 0x7;
    sample.tiny.a = (seed >> 1) & 0x1;
    sample.tiny.b = (seed >> 2) & 0x3;
    sample.tiny.c = (seed >> 4) & 0x7;
    sample.mid = (seed + 3) & 0x1f;
    sample.mixed.flag = (seed >> 3) & 0x1;
    sample.mixed.code = (seed + 5) & 0xf;
    sample.mixed.state = u<3>(seed + 2);
    sample.mixed.tail = (seed >> 5) & 0x3;
    sample.nibble = u<4>(seed ^ 0xa);
    sample.last = (seed >> 7) & 0x1;
    return sample;
}

static UnionContainingStructContainingUnion expected_union_struct(uint32_t seed)
{
    UnionContainingStructContainingUnion ret{};
    ret.wrapped.ua = seed & 0x1;
    ret.wrapped.nested.prefix = (seed >> 1) & 0x7;
    ret.wrapped.nested.inner.s.ia = (seed + 1) & 0x3;
    ret.wrapped.nested.inner.s.ib = u<5>(seed + 3);
    ret.wrapped.nested.inner.s.ic = (seed >> 2) & 0x7;
    ret.wrapped.ub = (seed + 5) & 0x3f;
    return ret;
}

static StructContainingUnionContainingStruct expected_struct_union(uint32_t seed)
{
    StructContainingUnionContainingStruct ret{};
    ret.head = (seed >> 2) & 0x3;
    ret.u.branch.us0 = seed & 0x3;
    ret.u.branch.nested.sa = (seed + 7) & 0xf;
    ret.u.branch.nested.sb = u<6>(seed + 11);
    ret.u.branch.nested.sc = (seed >> 6) & 0x1;
    ret.u.branch.us1 = (seed >> 3) & 0x7;
    ret.tail = (seed + 13) & 0x7f;
    return ret;
}

static StructWithEnum expected_enum_struct(uint32_t seed)
{
    StructWithEnum ret{};
    ret.prefix = seed & 0x3;
    ret.mode = (seed & 0x1) ? AlignMode::MODE_THREE : AlignMode::MODE_ONE;
    ret.tiny.a = (seed >> 1) & 0x1;
    ret.tiny.b = (seed >> 2) & 0x3;
    ret.tiny.c = (seed >> 4) & 0x7;
    ret.suffix = (seed + 3) & 0x7;
    return ret;
}

template<typename T>
static void print_bytes(const char* name, const T& sample)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&sample);
    std::print("{}:", name);
    for (size_t i = 0; i < sizeof(sample); ++i) {
        std::print(" {:02x}", bytes[i]);
    }
    std::print("\n");
}

static std::filesystem::path generated_sv_path(const std::string& file)
{
    std::filesystem::path copied = std::filesystem::path("StructAlignment_1") / file;
    std::filesystem::path generated = std::filesystem::path("generated") / file;
    if (std::filesystem::exists(copied) && (!std::filesystem::exists(generated) ||
            std::filesystem::last_write_time(copied) >= std::filesystem::last_write_time(generated))) {
        return copied;
    }
    return generated;
}

static bool file_has_import(const std::filesystem::path& path, const std::string& package)
{
    std::ifstream in(path);
    if (!in) {
        std::print("\nERROR: can't open generated SystemVerilog file {}\n", path.string());
        return false;
    }

    const std::string needle = "import " + package + "_pkg::*;";
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text.find(needle) != std::string::npos;
}

static bool check_imports(const std::string& file, const std::vector<std::string>& packages)
{
    bool ok = true;
    const auto path = generated_sv_path(file);
    for (const auto& package : packages) {
        if (!file_has_import(path, package)) {
            std::print("\nERROR: {} does not import {}_pkg::*\n", path.string(), package);
            ok = false;
        }
    }
    return ok;
}

static bool generated_sv_has_struct_package_imports()
{
    bool ok = true;
    ok &= check_imports("OuterBits_pkg.sv", {"TinyBits", "MixedBits"});
    ok &= check_imports("StructWithUnion_pkg.sv", {"InnerUnion"});
    ok &= check_imports("UnionContainingStructContainingUnion_pkg.sv", {"StructWithUnion", "InnerUnion"});
    ok &= check_imports("UnionWithStruct_pkg.sv", {"UnionStruct"});
    ok &= check_imports("StructContainingUnionContainingStruct_pkg.sv", {"UnionWithStruct", "UnionStruct"});
    ok &= check_imports("StructWithEnum_pkg.sv", {"AlignMode_MODES", "TinyBits"});
    ok &= check_imports("StructAlignment.sv", {
        "AlignMode_MODES",
        "TinyBits",
        "MixedBits",
        "OuterBits",
        "InnerUnion",
        "StructWithUnion",
        "UnionContainingStructContainingUnion",
        "UnionStruct",
        "UnionWithStruct",
        "StructContainingUnionContainingStruct",
        "StructWithEnum"
    });
    return ok;
}

class TestStructAlignment : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    StructAlignment dut;
#endif

    u<8> seed;
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

    OuterBits sample()
    {
#ifdef VERILATOR
        OuterBits ret{};
        std::memcpy(&ret, &dut.sample_out, sizeof(ret));
        return ret;
#else
        return dut.sample_out();
#endif
    }

    UnionContainingStructContainingUnion union_struct()
    {
#ifdef VERILATOR
        UnionContainingStructContainingUnion ret{};
        std::memcpy(&ret, &dut.union_struct_out, sizeof(ret));
        return ret;
#else
        return dut.union_struct_out();
#endif
    }

    StructContainingUnionContainingStruct struct_union()
    {
#ifdef VERILATOR
        StructContainingUnionContainingStruct ret{};
        std::memcpy(&ret, &dut.struct_union_out, sizeof(ret));
        return ret;
#else
        return dut.struct_union_out();
#endif
    }

    StructWithEnum enum_struct()
    {
#ifdef VERILATOR
        StructWithEnum ret{};
        std::memcpy(&ret, &dut.enum_struct_out, sizeof(ret));
        return ret;
#else
        return dut.enum_struct_out();
#endif
    }

    bool same_fields(const OuterBits& got, const OuterBits& exp)
    {
        return got.head == exp.head &&
            got.tiny.a == exp.tiny.a &&
            got.tiny.b == exp.tiny.b &&
            got.tiny.c == exp.tiny.c &&
            got.mid == exp.mid &&
            got.mixed.flag == exp.mixed.flag &&
            got.mixed.code == exp.mixed.code &&
            got.mixed.state == exp.mixed.state &&
            got.mixed.tail == exp.mixed.tail &&
            got.nibble == exp.nibble &&
            got.last == exp.last;
    }

    bool same_fields(const UnionContainingStructContainingUnion& got, const UnionContainingStructContainingUnion& exp)
    {
        return got.wrapped.ua == exp.wrapped.ua &&
            got.wrapped.nested.prefix == exp.wrapped.nested.prefix &&
            got.wrapped.nested.inner.s.ia == exp.wrapped.nested.inner.s.ia &&
            got.wrapped.nested.inner.s.ib == exp.wrapped.nested.inner.s.ib &&
            got.wrapped.nested.inner.s.ic == exp.wrapped.nested.inner.s.ic &&
            got.wrapped.ub == exp.wrapped.ub;
    }

    bool same_fields(const StructContainingUnionContainingStruct& got, const StructContainingUnionContainingStruct& exp)
    {
        return got.head == exp.head &&
            got.u.branch.us0 == exp.u.branch.us0 &&
            got.u.branch.nested.sa == exp.u.branch.nested.sa &&
            got.u.branch.nested.sb == exp.u.branch.nested.sb &&
            got.u.branch.nested.sc == exp.u.branch.nested.sc &&
            got.u.branch.us1 == exp.u.branch.us1 &&
            got.tail == exp.tail;
    }

    bool same_fields(const StructWithEnum& got, const StructWithEnum& exp)
    {
        return got.prefix == exp.prefix &&
            got.mode == exp.mode &&
            got.tiny.a == exp.tiny.a &&
            got.tiny.b == exp.tiny.b &&
            got.tiny.c == exp.tiny.c &&
            got.suffix == exp.suffix;
    }

    template<typename T>
    void check_fields(const char* name, uint32_t value, const T& got, const T& exp)
    {
        if (!same_fields(got, exp)) {
            std::print("\nseed {} {} field mismatch\n", value, name);
            print_bytes("got     ", got);
            print_bytes("expected", exp);
            error = true;
        }
    }

    void check(uint32_t value)
    {
        check_fields("struct", value, sample(), expected_sample(value));
        check_fields("union_struct", value, union_struct(), expected_union_struct(value));
        check_fields("struct_union", value, struct_union(), expected_struct_union(value));
        check_fields("enum_struct", value, enum_struct(), expected_enum_struct(value));
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestStructAlignment...");
#else
        std::print("CppHDL TestStructAlignment...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "struct_alignment_test";
        _assign();

        seed = 0;
        eval(true);
        neg(true);
        ++sys_clock;

        for (uint32_t i = 0; i < 256 && !error; ++i) {
            seed = i;
            eval(false);
            check(i);
            neg(false);
            ++sys_clock;
        }

        std::print(" {} ({} us, sizes tiny/mixed/outer/union_struct/struct_union/enum_struct={}/{}/{}/{}/{}/{})\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count(),
            sizeof(TinyBits), sizeof(MixedBits), sizeof(OuterBits),
            sizeof(UnionContainingStructContainingUnion), sizeof(StructContainingUnionContainingStruct),
            sizeof(StructWithEnum));
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
        ok &= VerilatorCompile(__FILE__, "StructAlignment", {
            "Predef_pkg",
            "AlignMode_MODES_pkg",
            "TinyBits_pkg",
            "MixedBits_pkg",
            "OuterBits_pkg",
            "InnerUnion_pkg",
            "StructWithUnion_pkg",
            "UnionContainingStructContainingUnion_pkg",
            "UnionStruct_pkg",
            "UnionWithStruct_pkg",
            "StructContainingUnionContainingStruct_pkg",
            "StructWithEnum_pkg"
        }, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("StructAlignment_1/obj_dir/VStructAlignment") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
    ok &= generated_sv_has_struct_package_imports();
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestStructAlignment().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
