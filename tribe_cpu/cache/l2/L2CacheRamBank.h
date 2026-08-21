#pragma once

#include "cpphdl.h"

using namespace cpphdl;

// One synchronous L2 storage bank.  Cache policy, addressing and arbitration
// remain in the CppHDL L2Cache controller; only physical RAM inference is
// delegated to a small leaf primitive.
template<size_t WIDTH, size_t DEPTH>
class [[clang::annotate("CPPHDL_REPLACEMENT_FILE=L2CacheRamBankPrimitive.sv;")]]
L2CacheRamBank : public Module
{
public:
    _PORT(u<clog2(DEPTH)>) addr_in;
    _PORT(bool) wr_in;
    _PORT(bool) rd_in;
    _PORT(logic<WIDTH>) data_in;
    _PORT(logic<WIDTH>) data_out = _ASSIGN_REG(data_out_reg);

private:
    memory<logic<WIDTH>, 1, DEPTH> buffer;
    reg<logic<WIDTH>> data_out_reg;

public:
    void _work(bool) {}
    void _strobe() {}

    void _work_l2_clock(bool)
    {
        if (wr_in()) {
            buffer[addr_in()] = data_in();
        }
        if (rd_in()) {
            data_out_reg._next = buffer[addr_in()];
        }
    }

    void _strobe_l2_clock()
    {
        buffer.apply();
        data_out_reg.strobe();
    }

#ifndef SYNTHESIS
    void checkpoint_l2(FILE* checkpoint_fd)
    {
        buffer.apply(checkpoint_fd);
        data_out_reg.strobe(checkpoint_fd);
    }
#endif

    void _assign() {}
};
