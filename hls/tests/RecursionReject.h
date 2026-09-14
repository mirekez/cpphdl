#include "cpphdl.h"

#ifndef HLS_BAD_CASE
#define HLS_BAD_CASE 0
#endif

class RecursionReject : public cpphdl::Module
{
public:
    _PORT(uint32_t) n_in;
    _PORT(uint32_t) result_out;
private:
    cpphdl::reg<cpphdl::u32> result_reg;
#if HLS_BAD_CASE == 1
    [[clang::annotate("CPPHDL_HLS_MAX_RECURSION=0")]]
#endif
    uint32_t recurse(uint32_t n)
    {
        return n == 0 ? 0 : recurse(n - 1);
    }
#if HLS_BAD_CASE == 2
    uint32_t recurse_limit() { return 0; }
#elif HLS_BAD_CASE == 3
    uint32_t recurse_limit(uint32_t n) { return recurse(n); }
#elif HLS_BAD_CASE != 0
    uint32_t recurse_limit(uint32_t n) { return 0; }
#endif
#if HLS_BAD_CASE == 4
    uint32_t recurse_hls_0(uint32_t n) { return 0; }
#endif
public:
    void _assign() { result_out = _ASSIGN((uint32_t)result_reg); }
    void _work(bool reset)
    {
        if (reset) result_reg.clr();
        else result_reg._next = recurse(n_in());
    }
    void _strobe() { result_reg.strobe(); }
};
