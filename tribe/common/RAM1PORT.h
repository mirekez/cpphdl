#pragma once

#include "cpphdl.h"

using namespace cpphdl;

#ifndef DEBUG_RAM
#define DEBUG_RAM(...)
#endif

template<size_t WIDTH, size_t DEPTH>
class RAM1PORT : public Module
{
public:
    _PORT(u<clog2(DEPTH)>) addr_in;
    _PORT(logic<WIDTH>)    data_in;
    _PORT(bool)            wr_in;
    _PORT(bool)            rd_in;
    _PORT(logic<WIDTH>)    q_out = _ASSIGN_REG( q_out_reg );
    int id_in;

private:
    reg<logic<WIDTH>> q_out_reg;
    memory<u8,(WIDTH+7)/8,DEPTH> buffer;

public:

    void _work(bool reset)
    {
        if (reset) {
            q_out_reg.clr();
            return;
        }
        if (wr_in()) {
            buffer[addr_in()] = data_in();
        }
        if (rd_in()) {
            q_out_reg._next = buffer[addr_in()];
        }
        DEBUG_RAM("RAM {}, ({:d}){} @{} ({:d}){}, WIDTH: {}, DEPTH: {}\n", id_in, (bool)wr_in(), data_in(), addr_in(), (bool)rd_in(), q_out(), WIDTH, DEPTH);
    }

    void _strobe()
    {
        buffer.apply();
        q_out_reg.strobe();
    }
};
