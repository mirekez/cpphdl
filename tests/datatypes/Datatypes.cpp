#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

#define CPPHDL_U_WIDTHS(X) \
    X(1) X(8) X(16) X(24) X(32) X(40) X(48) X(56) X(64)

#define CPPHDL_LOGIC_WIDTHS(X) \
    X(1) X(8) X(16) X(24) X(32) X(40) X(48) X(56) X(64) \
    X(72) X(80) X(88) X(96) X(104) X(112) X(120) X(128) \
    X(136) X(144) X(152) X(160) X(168) X(176) X(184) X(192) \
    X(200) X(208) X(216) X(224) X(232) X(240) X(248) X(256) \
    X(264) X(272) X(280) X(288) X(296) X(304) X(312) X(320) \
    X(328) X(336) X(344) X(352) X(360) X(368) X(376) X(384) \
    X(392) X(400) X(408) X(416) X(424) X(432) X(440) X(448) \
    X(456) X(464) X(472) X(480) X(488) X(496) X(504) X(512)

#define CPPHDL_LOGIC_WIDTHS_GE8(X) \
    X(8) X(16) X(24) X(32) X(40) X(48) X(56) X(64) \
    X(72) X(80) X(88) X(96) X(104) X(112) X(120) X(128) \
    X(136) X(144) X(152) X(160) X(168) X(176) X(184) X(192) \
    X(200) X(208) X(216) X(224) X(232) X(240) X(248) X(256) \
    X(264) X(272) X(280) X(288) X(296) X(304) X(312) X(320) \
    X(328) X(336) X(344) X(352) X(360) X(368) X(376) X(384) \
    X(392) X(400) X(408) X(416) X(424) X(432) X(440) X(448) \
    X(456) X(464) X(472) X(480) X(488) X(496) X(504) X(512)

#define CPPHDL_BYTES_8(X) \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7)

#define CPPHDL_WORDS_4(X) \
    X(0) X(1) X(2) X(3)

#define CPPHDL_BYTES_64(X) \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) \
    X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) \
    X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) \
    X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) \
    X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) \
    X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63)

// CppHDL MODEL /////////////////////////////////////////////////////////

class Datatypes : public Module
{
public:
    _PORT(u<8>)       seed_in;
    _PORT(u<8>)       addr_in;
    _PORT(bool)       write_in;
    _PORT(logic<512>) digest_out = _ASSIGN_REG( digest_comb_func() );
    _PORT(logic<512>) memory_out = _ASSIGN_REG( memory_comb_func() );

private:
#define DECL_U(W) reg<u<W>> u##W##_reg;
    CPPHDL_U_WIDTHS(DECL_U)
#undef DECL_U

#define DECL_U_TMP(W) logic<512> u##W##_tmp;
    CPPHDL_U_WIDTHS(DECL_U_TMP)
#undef DECL_U_TMP

    reg<u1>  alias_u1_reg;
    reg<u8>  alias_u8_reg;
    reg<u16> alias_u16_reg;
    reg<u32> alias_u32_reg;
    reg<u64> alias_u64_reg;
    reg<i8>  alias_i8_reg;
    reg<i16> alias_i16_reg;
    reg<i32> alias_i32_reg;
    reg<i64> alias_i64_reg;

#define DECL_LOGIC(W) reg<logic<W>> logic##W##_reg;
    CPPHDL_LOGIC_WIDTHS(DECL_LOGIC)
#undef DECL_LOGIC

#define DECL_LOGIC_TMP(W) logic<512> logic##W##_tmp;
    CPPHDL_LOGIC_WIDTHS(DECL_LOGIC_TMP)
#undef DECL_LOGIC_TMP

    reg<array<u8,8>> array_u8_reg;
    reg<array<u16,4>> array_u16_reg;
    reg<array<logic<32>,4>> array_logic_reg;

    memory<u8,64,16> byte_memory;
    logic<512> digest_comb;
    logic<512> memory_comb;

    logic<512>& digest_comb_func()
    {
        digest_comb = 0;

#define FOLD_U(W) \
        u##W##_tmp = 0; \
        u##W##_tmp.bits(W - 1, 0) = u##W##_reg; \
        digest_comb.bits(W - 1, 0) = u##W##_reg; \
        digest_comb = digest_comb ^ (u##W##_tmp << ((W * 3) % 64));
        CPPHDL_U_WIDTHS(FOLD_U)
#undef FOLD_U

        digest_comb.bits(7,0)     = alias_u8_reg;
        digest_comb.bits(23,8)    = alias_u16_reg;
        digest_comb.bits(55,24)   = alias_u32_reg;
        digest_comb.bits(119,56)  = alias_u64_reg;
        digest_comb.bits(127,120) = alias_i8_reg;
        digest_comb.bits(143,128) = alias_i16_reg;
        digest_comb.bits(175,144) = alias_i32_reg;
        digest_comb.bits(239,176) = alias_i64_reg;

#define FOLD_LOGIC(W) \
        logic##W##_tmp = 0; \
        logic##W##_tmp.bits(W - 1, 0) = logic##W##_reg; \
        digest_comb = digest_comb ^ (logic##W##_tmp << ((W / 8) % 64));
        CPPHDL_LOGIC_WIDTHS(FOLD_LOGIC)
#undef FOLD_LOGIC

        digest_comb.bits(63,0) = digest_comb.bits(63,0) ^ array_u8_reg;
        digest_comb.bits(127,64) = digest_comb.bits(127,64) ^ array_u16_reg;
        digest_comb.bits(255,128) = digest_comb.bits(255,128) ^ array_logic_reg;
        return digest_comb;
    }

    logic<512>& memory_comb_func()
    {
        memory_comb = byte_memory[(uint64_t)addr_in() & 0xf];
        return memory_comb;
    }

public:
    void _work(bool reset)
    {
        if (reset) {
#define RESET_U(W) u##W##_reg.clr();
            CPPHDL_U_WIDTHS(RESET_U)
#undef RESET_U
            alias_u1_reg.clr();
            alias_u8_reg.clr();
            alias_u16_reg.clr();
            alias_u32_reg.clr();
            alias_u64_reg.clr();
            alias_i8_reg.clr();
            alias_i16_reg.clr();
            alias_i32_reg.clr();
            alias_i64_reg.clr();
#define RESET_LOGIC(W) logic##W##_reg.clr();
            CPPHDL_LOGIC_WIDTHS(RESET_LOGIC)
#undef RESET_LOGIC
            array_u8_reg.clr();
            array_u16_reg.clr();
            array_logic_reg.clr();
            return;
        }

#define WORK_U(W) \
        u##W##_reg._next = u<W>((uint64_t)seed_in() + W); \
        u##W##_reg._next = u##W##_reg._next - u<W>(W / 8); \
        u##W##_reg._next = u##W##_reg._next + u<W>(((uint64_t)u##W##_reg._next >> (W - 1)) & 1);
        CPPHDL_U_WIDTHS(WORK_U)
#undef WORK_U

        alias_u1_reg._next = u1((uint64_t)seed_in() & 1);
        alias_u8_reg._next = u8((uint64_t)seed_in() + 0x11);
        alias_u16_reg._next = u16((uint64_t)seed_in() * 3 + 0x1234);
        alias_u32_reg._next = u32((uint64_t)seed_in() * 5 + 0x12345678);
        alias_u64_reg._next = u64((uint64_t)seed_in() * 7 + 0x123456789abcdef0ULL);
        alias_i8_reg._next = i8((int8_t)((int)seed_in() - 100));
        alias_i16_reg._next = i16((int16_t)((int)seed_in() * 2 - 1000));
        alias_i32_reg._next = i32((int32_t)((int)seed_in() * 3 - 100000));
        alias_i64_reg._next = i64((int64_t)((int)seed_in() * 4 - 10000000000LL));

#define WORK_LOGIC(W) \
        logic##W##_reg._next = logic<W>(seed_in()) + logic<W>(W); \
        logic##W##_reg._next = logic##W##_reg._next - logic<W>(W / 8); \
        logic##W##_reg._next[0] = ((uint64_t)seed_in() + W) & 1; \
        logic##W##_reg._next.bits(7, 0) = (uint64_t)(0xa5 + W);
        logic1_reg._next = logic<1>(seed_in()) + logic<1>(1);
        logic1_reg._next = logic1_reg._next - logic<1>(0);
        logic1_reg._next[0] = ((uint64_t)seed_in() + 1) & 1;
        logic1_reg._next.bits(0, 0) = (uint64_t)(0xa5 + 1);
        CPPHDL_LOGIC_WIDTHS_GE8(WORK_LOGIC)
#undef WORK_LOGIC

#define WORK_ARRAY8(I) array_u8_reg._next[I] = (uint8_t)((uint64_t)seed_in() + I);
        CPPHDL_BYTES_8(WORK_ARRAY8)
#undef WORK_ARRAY8
#define WORK_ARRAY4(I) \
        array_u16_reg._next[I] = u16((uint64_t)seed_in() + 0x100 + I); \
        array_logic_reg._next[I] = logic<32>((uint64_t)seed_in() + 0x10000 + I);
        CPPHDL_WORDS_4(WORK_ARRAY4)
#undef WORK_ARRAY4

        if (write_in()) {
            logic<512> mem_word;
            mem_word = 0;
#define WORK_MEM_BYTE(I) mem_word.bits(I * 8 + 7, I * 8) = (uint64_t)seed_in() + I;
            CPPHDL_BYTES_64(WORK_MEM_BYTE)
#undef WORK_MEM_BYTE
            byte_memory[(uint64_t)addr_in() & 0xf] = mem_word;
        }
    }

    void _strobe()
    {
        byte_memory.apply();
#define STROBE_U(W) u##W##_reg.strobe();
        CPPHDL_U_WIDTHS(STROBE_U)
#undef STROBE_U
        alias_u1_reg.strobe();
        alias_u8_reg.strobe();
        alias_u16_reg.strobe();
        alias_u32_reg.strobe();
        alias_u64_reg.strobe();
        alias_i8_reg.strobe();
        alias_i16_reg.strobe();
        alias_i32_reg.strobe();
        alias_i64_reg.strobe();
#define STROBE_LOGIC(W) logic##W##_reg.strobe();
        CPPHDL_LOGIC_WIDTHS(STROBE_LOGIC)
#undef STROBE_LOGIC
        array_u8_reg.strobe();
        array_u16_reg.strobe();
        array_logic_reg.strobe();
    }

    void _assign() {}
};
/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

static logic<512> expected_memory(uint8_t seed)
{
    logic<512> ret;
    ret = 0;
    for (size_t i=0; i < 64; ++i) {
        ret.bits(i * 8 + 7, i * 8) = (uint64_t)seed + i;
    }
    return ret;
}

static logic<512> expected_digest(uint8_t seed)
{
    logic<512> digest;
    digest = 0;

#define EXPECT_U(W) \
    { \
        u<W> v = u<W>((uint64_t)seed + W); \
        v = v - u<W>(W / 8); \
        v = v + u<W>(((uint64_t)v >> (W - 1)) & 1); \
        digest.bits(W - 1, 0) = v; \
        digest = digest ^ (logic<512>(v) << ((W * 3) % 64)); \
    }
    CPPHDL_U_WIDTHS(EXPECT_U)
#undef EXPECT_U

    digest.bits(7,0)     = u8((uint64_t)seed + 0x11);
    digest.bits(23,8)    = u16((uint64_t)seed * 3 + 0x1234);
    digest.bits(55,24)   = u32((uint64_t)seed * 5 + 0x12345678);
    digest.bits(119,56)  = u64((uint64_t)seed * 7 + 0x123456789abcdef0ULL);
    digest.bits(127,120) = i8((int8_t)((int)seed - 100));
    digest.bits(143,128) = i16((int16_t)((int)seed * 2 - 1000));
    digest.bits(175,144) = i32((int32_t)((int)seed * 3 - 100000));
    digest.bits(239,176) = i64((int64_t)((int)seed * 4 - 10000000000LL));

#define EXPECT_LOGIC_GE8(W) \
    { \
        logic<W> v = logic<W>(seed) + logic<W>(W); \
        v = v - logic<W>(W / 8); \
        v[0] = ((uint64_t)seed + W) & 1; \
        v.bits(7, 0) = (uint64_t)(0xa5 + W); \
        logic<512> tmp; \
        tmp = 0; \
        tmp.bits(W - 1, 0) = v; \
        digest = digest ^ (tmp << ((W / 8) % 64)); \
    }
    {
        logic<1> v = logic<1>(seed) + logic<1>(1);
        v = v - logic<1>(0);
        v[0] = ((uint64_t)seed + 1) & 1;
        v.bits(0, 0) = (uint64_t)(0xa5 + 1);
        logic<512> tmp;
        tmp = 0;
        tmp.bits(0, 0) = v;
        digest = digest ^ (tmp << 0);
    }
    CPPHDL_LOGIC_WIDTHS_GE8(EXPECT_LOGIC_GE8)
#undef EXPECT_LOGIC_GE8

    array<u8,8> arr8;
    array<u16,4> arr16;
    array<logic<32>,4> arrlogic;
    for (size_t i=0; i < 8; ++i) {
        arr8[i] = (uint8_t)(seed + i);
    }
    for (size_t i=0; i < 4; ++i) {
        arr16[i] = u16((uint64_t)seed + 0x100 + i);
        arrlogic[i] = logic<32>((uint64_t)seed + 0x10000 + i);
    }
    digest.bits(63,0) = digest.bits(63,0) ^ arr8;
    digest.bits(127,64) = digest.bits(127,64) ^ arr16;
    digest.bits(255,128) = digest.bits(255,128) ^ arrlogic;
    return digest;
}

template<typename T>
static T verilator_read(const void* ptr)
{
    T value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

class TestDatatypes : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    Datatypes dut;
#endif

    u<8> seed;
    u<8> addr;
    bool write = false;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        dut.seed_in = _ASSIGN_REG( seed );
        dut.addr_in = _ASSIGN_REG( addr );
        dut.write_in = _ASSIGN_REG( write );
        dut.__inst_name = __inst_name + "/dut";
        dut._assign();
#endif
    }

    void eval_pos(bool reset)
    {
#ifdef VERILATOR
        dut.seed_in = seed;
        dut.addr_in = addr;
        dut.write_in = write;
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

    logic<512> read_digest()
    {
#ifdef VERILATOR
        return verilator_read<logic<512>>(&dut.digest_out);
#else
        return dut.digest_out();
#endif
    }

    logic<512> read_memory()
    {
#ifdef VERILATOR
        return verilator_read<logic<512>>(&dut.memory_out);
#else
        return dut.memory_out();
#endif
    }

    bool check(const char* name, const logic<512>& got, const logic<512>& exp)
    {
        if (got != exp) {
            std::print("\n{} ERROR: got {}, expected {}\n", name, got, exp);
            error = true;
        }
        return !error;
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestDatatypes...");
#else
        std::print("CppHDL TestDatatypes...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "datatypes_test";
        _assign();

        seed = 0;
        addr = 0;
        write = false;
        eval_pos(true);
        _strobe();
        eval_neg(true);
        ++sys_clock;

        for (uint8_t i=0; i < 32; ++i) {
            seed = (uint8_t)(i * 7 + 3);
            addr = (uint8_t)(i & 0xf);
            write = true;
            eval_pos(false);
            _strobe();
            eval_neg(false);
            ++sys_clock;

            write = false;
            eval_pos(false);
            logic<512> got_digest = read_digest();
            logic<512> got_memory = read_memory();
            check("digest", got_digest, expected_digest(seed));
            check("memory", got_memory, expected_memory(seed));
            if (error) {
                break;
            }
            eval_neg(false);
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
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "Datatypes", {"Predef_pkg"}, {"../../../../include"}, 1);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("Datatypes_1/obj_dir/VDatatypes") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestDatatypes().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
