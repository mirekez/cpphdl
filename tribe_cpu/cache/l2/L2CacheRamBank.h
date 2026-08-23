#pragma once

#include "cpphdl.h"

using namespace cpphdl;

// Synchronous one-port storage leaf used by the generated L2 controller.
// Keeping only the physical RAM shape in a replacement prevents the CppHDL
// packed bank arrays from being elaborated as hundreds of thousands of FFs.
template<size_t WIDTH, size_t DEPTH>
class [[clang::annotate("CPPHDL_REPLACEMENT_FILE=L2CacheRamBankPrimitive.sv;")]]
L2CacheRamBank : public Module
{
public:
    _PORT(u<clog2(DEPTH)>) addr_in;
    _PORT(bool) write_in;
    _PORT(bool) read_in;
    _PORT(logic<WIDTH>) write_data_in;
    _PORT(logic<WIDTH>) read_data_out;

private:
    memory<logic<WIDTH>, 1, DEPTH> buffer;
    reg<logic<WIDTH>> read_data_reg;

public:
    void _assign()
    {
        read_data_out = _ASSIGN_REG(read_data_reg);
    }

    void _work(bool) {}

    void _work_l2_clock(bool reset)
    {
        if (reset) {
            read_data_reg.clr();
            return;
        }
        if (write_in()) buffer[addr_in()] = write_data_in();
        if (read_in()) read_data_reg._next = buffer[addr_in()];
    }

    void _strobe() {}

    void _strobe_l2_clock()
    {
        buffer.apply();
        read_data_reg.strobe();
    }

#ifndef SYNTHESIS
    // The legacy L2 stream stores every RAM image before any registered RAM
    // output.  Keep the two pieces independently serializable so replacing the
    // packed RAM array with bank modules does not alter that byte order.
    void checkpoint_memory_l2(FILE* checkpoint_fd)
    {
        buffer.apply(checkpoint_fd);
    }

    void checkpoint_read_data_current_l2(FILE* checkpoint_fd)
    {
        if (!checkpoint_reading(checkpoint_fd)) {
            read_data_reg.strobe();
        }
        checkpoint_value(checkpoint_fd,
            static_cast<logic<WIDTH>&>(read_data_reg));
    }

    void checkpoint_read_data_next_l2(FILE* checkpoint_fd)
    {
        checkpoint_value(checkpoint_fd, read_data_reg._next);
    }
#endif
};

template class L2CacheRamBank<32, 128>;
