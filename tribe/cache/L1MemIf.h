#pragma once

#include "cpphdl.h"

using namespace cpphdl;

template<size_t PORT_BITWIDTH>
struct L1MemIf : public Interface
{
    _PORT(bool) read_in;
    _PORT(bool) write_in;
    _PORT(uint32_t) addr_in;
    _PORT(uint32_t) write_data_in;
    _PORT(uint8_t) write_mask_in;
    _PORT(logic<PORT_BITWIDTH>) read_data_out;
    _PORT(bool) wait_out;
};
