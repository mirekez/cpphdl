#include "cpphdl.h"

#if defined(__clang__)
#define HLS_BOUND [[clang::annotate("CPPHDL_HLS_MAX_RECURSION=4")]]
#else
#define HLS_BOUND
#endif

template<unsigned STEP>
struct HlsRecursiveHelper
{
    static uint64_t walk_limit(uint32_t n, uint32_t budget)
    {
        return n == 0 ? 0 : 0x100000000ULL;
    }
    HLS_BOUND static uint64_t walk(uint32_t n, uint32_t budget)
    {
        uint64_t tail;
        if (n == 0 || budget == 0) return walk_limit(n, budget);
        tail = walk(n - 1, budget - 1);
        return tail >> 32 ? tail : tail + STEP;
    }
};

class HlsRecursiveLeaf : public cpphdl::Module
{
public:
    uint64_t count_limit(uint32_t n, uint32_t budget)
    {
        return n == 0 ? 0 : 0x100000000ULL;
    }
    HLS_BOUND uint64_t count(uint32_t n, uint32_t budget)
    {
        uint64_t tail;
        if (n == 0 || budget == 0) return count_limit(n, budget);
        tail = count(n - 1, budget - 1);
        return tail >> 32 ? tail : tail + 1;
    }
    void _work(bool) {}
};

class Recursion : public cpphdl::Module
{
    HlsRecursiveLeaf leaf;
public:
    _PORT(uint32_t) n_in;
    _PORT(uint32_t) mode_in;
    _PORT(uint64_t) result_out;
private:
    cpphdl::reg<cpphdl::u64> result_reg;
    uint64_t sum_limit(uint32_t n, uint32_t budget)
    {
        return n == 0 ? 0 : 0x100000000ULL;
    }
    HLS_BOUND uint64_t sum(uint32_t n, uint32_t budget)
    {
        uint64_t rest;
        if (n == 0 || budget == 0) return sum_limit(n, budget);
        rest = sum(n - 1, budget - 1);
        if (rest >> 32) return rest;
        return rest + n;
    }
    uint64_t default_sum_limit(uint32_t n, uint32_t budget)
    {
        return n == 0 ? 0 : 0x100000000ULL;
    }
    uint64_t default_sum(uint32_t n, uint32_t budget)
    {
        uint64_t tail;
        if (n == 0 || budget == 0) return default_sum_limit(n, budget);
        tail = default_sum(n - 1, budget - 1);
        return tail >> 32 ? tail : tail + n;
    }
    uint64_t even_limit(uint32_t n, uint32_t budget)
    {
        return n == 0 ? 1 : 0x100000000ULL;
    }
    uint64_t odd_limit(uint32_t n, uint32_t budget)
    {
        return n == 0 ? 0 : 0x100000000ULL;
    }
    HLS_BOUND uint64_t even(uint32_t n, uint32_t budget)
    {
        if (n == 0 || budget == 0) return even_limit(n, budget);
        return odd(n - 1, budget - 1);
    }
    HLS_BOUND uint64_t odd(uint32_t n, uint32_t budget)
    {
        if (n == 0 || budget == 0) return odd_limit(n, budget);
        return even(n - 1, budget - 1);
    }
    uint64_t tree_limit(uint32_t n, uint32_t budget)
    {
        return n < 2 ? n : 0x100000000ULL;
    }
    HLS_BOUND uint64_t tree(uint32_t n, uint32_t budget)
    {
        uint64_t left, right;
        if (n < 2 || budget == 0) return tree_limit(n, budget);
        left = tree(n - 1, budget - 1);
        right = tree(n - 2, budget - 1);
        return (left >> 32) || (right >> 32) ? 0x100000000ULL : left + right;
    }
public:
    void _assign() { result_out = _ASSIGN((uint64_t)result_reg); leaf._assign(); }
    void _work(bool reset)
    {
        if (reset) result_reg.clr();
        else {
            switch (mode_in()) {
            case 0: result_reg._next = sum(n_in(), 4); break;
            case 1: result_reg._next = default_sum(n_in(), 10); break;
            case 2: result_reg._next = even(n_in(), 4); break;
            case 3: result_reg._next = tree(n_in(), 4); break;
            case 4: result_reg._next = HlsRecursiveHelper<7>::walk(n_in(), 4); break;
            default: result_reg._next = leaf.count(n_in(), 4); break;
            }
        }
        leaf._work(reset);
    }
    void _strobe() { result_reg.strobe(); leaf._strobe(); }
};

#ifndef SYNTHESIS
#include <cstdio>
#ifdef VERILATOR
#include "VRecursion.h"
#endif
long _system_clock = 0;
int main()
{
    uint32_t n = 0, mode = 0;
#ifdef VERILATOR
    VRecursion dut;
#else
    Recursion dut;
    dut.n_in = _ASSIGN(n);
    dut.mode_in = _ASSIGN(mode);
    dut._assign();
#endif
    for (unsigned cycle = 0; cycle < 90; ++cycle) {
        const bool reset = cycle == 0;
        n = cycle % 15;
        mode = cycle / 15;
#ifdef VERILATOR
        dut.clk = 0; dut.reset = reset; dut.n_in = n; dut.mode_in = mode; dut.eval();
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
        const uint64_t result = dut.result_out;
#else
        dut._work(reset); dut._strobe();
        ++_system_clock;
        const uint64_t result = dut.result_out();
#endif
        uint64_t expected = 0x100000000ULL;
        if (mode == 0 && n <= 4) expected = n * (n + 1) / 2;
        if (mode == 1 && n <= 10) expected = n * (n + 1) / 2;
        if (mode == 2 && n <= 4) expected = (n % 2) == 0;
        if (mode == 3 && n <= 5) {
            uint64_t a = 0, b = 1;
            for (uint32_t i = 0; i < n; ++i) { const uint64_t next = a + b; a = b; b = next; }
            expected = a;
        }
        if (mode == 4 && n <= 4) expected = n * 7;
        if (mode == 5 && n <= 4) expected = n;
        if (reset) expected = 0;
        if (result != expected) { std::fprintf(stderr,"recursion mode=%u n=%u got=%llx expected=%llx\n",mode,n,(unsigned long long)result,(unsigned long long)expected); return 1; }
    }
    std::puts("PASS bounded recursive sum, including explicit overflow");
}
#endif
