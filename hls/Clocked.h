#pragma once
#include "cpphdl.h"
#include <new>

namespace cpphdl::hls {

// The native wrapper is a transaction reference; the AST pass schedules the
// actual object methods for RTL. T is not itself required to be an RTL module.
template<class T, unsigned MAX_RECURSION = 0, unsigned ADDRESS_BITS = 64>
class
#ifdef __clang__
[[clang::annotate("CPPHDL_HLS_CLOCKED")]]
#endif
Clocked : public cpphdl::Module {
    static_assert(ADDRESS_BITS >= 8 && ADDRESS_BITS <= 64, "ADDRESS_BITS must be in 8..64");
public:
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
private:
    T object{};
    cpphdl::reg<cpphdl::u1> pending_reg;
    cpphdl::reg<cpphdl::u64> result_reg;
public:
    void _assign() {
        command_ready_out = _ASSIGN(!pending_reg);
        response_valid_out = _ASSIGN((bool)pending_reg);
        result_out = _ASSIGN((uint64_t)result_reg);
        fault_out = _ASSIGN(0u);
    }
    void _work(bool reset) {
        if (reset) {
            object.~T();
            ::new (static_cast<void*>(&object)) T{};
            pending_reg.clr(); result_reg.clr();
        } else if (pending_reg) {
            if (response_ready_in()) pending_reg._next = false;
        } else if (command_valid_in()) {
            result_reg._next = object.command(operation_in(), index_in(), value_in());
            pending_reg._next = true;
        }
    }
    void _strobe() { pending_reg.strobe(); result_reg.strobe(); }
};
}
