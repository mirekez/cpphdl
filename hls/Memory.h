#pragma once

#include "cpphdl.h"

// One outstanding word transaction, including an acknowledgement for writes.
struct HlsMemoryIf : public cpphdl::Interface
{
    _PORT(bool) valid_in;
    _PORT(bool) write_in;
    _PORT(uint32_t) addr_in;
    _PORT(uint64_t) data_in;
    _PORT(bool) ready_out;
    _PORT(bool) valid_out;
    _PORT(uint64_t) data_out;
    _PORT(bool) ready_in;
};

template<unsigned CAPACITY>
class HlsSram : public cpphdl::Module
{
public:
    HlsMemoryIf memory_in;
    _PORT(bool) enable_in;
    _PORT(uint32_t) delay_in;
private:
    cpphdl::memory<cpphdl::u64, 1, CAPACITY> words;
    cpphdl::reg<cpphdl::u32> phase_reg, delay_reg;
    cpphdl::reg<cpphdl::u64> result_reg;
public:
    void _assign()
    {
        memory_in.ready_out = _ASSIGN(phase_reg == 0 && enable_in());
        memory_in.valid_out = _ASSIGN(phase_reg == 2);
        memory_in.data_out = _ASSIGN((uint64_t)result_reg);
    }
    void _work(bool reset)
    {
        if (reset) {
            phase_reg.clr(); delay_reg.clr(); result_reg.clr();
        } else {
            if (phase_reg == 0 && enable_in() && memory_in.valid_in()) {
                if (memory_in.write_in()) {
                    words[memory_in.addr_in()] = memory_in.data_in();
                    result_reg._next = 0;
                } else result_reg._next = (uint64_t)words[memory_in.addr_in()];
                delay_reg._next = delay_in();
                phase_reg._next = 1;
            } else if (phase_reg == 1) {
                if (delay_reg == 0) phase_reg._next = 2;
                else delay_reg._next = (uint32_t)delay_reg - 1;
            } else if (phase_reg == 2 && memory_in.ready_in()) phase_reg._next = 0;
        }
    }
    void _strobe()
    {
        words.apply();
        phase_reg.strobe(); delay_reg.strobe(); result_reg.strobe();
    }
};
