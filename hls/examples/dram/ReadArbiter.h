#pragma once
#include "../../ExternalMemory.h"

// Four one-outstanding masters share an ordered controller channel. The FIFO
// records response owners, not data. A blocked request retains its owner.
class ReadArbiter : public cpphdl::Module {
public:
    cpphdl::hls::ExternalMemoryIf<> clients_in[4];
    cpphdl::hls::ExternalMemoryIf<> memory_out;
private:
    cpphdl::reg<cpphdl::u<2>> turn_reg, head_reg, tail_reg;
    cpphdl::reg<cpphdl::u<3>> count_reg;
    cpphdl::reg<cpphdl::array<4, cpphdl::u<2>>> owners_reg;
    uint32_t owner_comb;
    const uint32_t& owner_comb_func() {
        unsigned i;
        owner_comb = 0;
        for (i = 0; i < 4; ++i) if (head_reg == i) owner_comb = uint32_t(owners_reg[i]);
        return owner_comb;
    }
    bool request_comb, response_ready_comb;
    uint32_t address_comb;
    const bool& request_comb_func() {
        unsigned i;
        request_comb = false;
        for (i = 0; i < 4; ++i) if (turn_reg == i) request_comb = clients_in[i].valid_in();
        return request_comb;
    }
    const uint32_t& address_comb_func() {
        unsigned i;
        address_comb = 0;
        for (i = 0; i < 4; ++i) if (turn_reg == i) address_comb = uint32_t(clients_in[i].addr_in());
        return address_comb;
    }
    const bool& response_ready_comb_func() {
        unsigned i;
        response_ready_comb = false;
        for (i = 0; i < 4; ++i)
            if (owner_comb_func() == i) response_ready_comb = clients_in[i].ready_in();
        return response_ready_comb;
    }
public:
    void _assign() {
        unsigned i;
        memory_out.valid_in = _ASSIGN(count_reg < 4 && request_comb_func());
        memory_out.write_in = _ASSIGN(false);
        memory_out.addr_in = _ASSIGN(address_comb_func());
        memory_out.size_in = _ASSIGN(uint8_t(8));
        memory_out.data_in = _ASSIGN(uint64_t(0));
        memory_out.ready_in = _ASSIGN(count_reg != 0 && response_ready_comb_func());
        for (i = 0; i < 4; ++i) {
            clients_in[i].ready_out = _ASSIGN_I(count_reg < 4 && turn_reg == i && memory_out.ready_out());
            clients_in[i].valid_out = _ASSIGN_I(count_reg != 0 && owner_comb_func() == i && memory_out.valid_out());
            clients_in[i].data_out = _ASSIGN(memory_out.data_out());
            clients_in[i].error_out = _ASSIGN(memory_out.error_out());
        }
    }
    void _work(bool reset) {
        unsigned i;
        bool push, pop;
        push = memory_out.valid_in() && memory_out.ready_out();
        pop = memory_out.valid_out() && memory_out.ready_in();
        if (reset) {
            turn_reg.clr(); head_reg.clr(); tail_reg.clr(); count_reg.clr(); owners_reg.clr();
        } else {
            if (!request_comb_func() || push) turn_reg._next = uint32_t(turn_reg) + 1u;
            if (push) {
                for (i = 0; i < 4; ++i) if (tail_reg == i) owners_reg._next[i] = turn_reg;
                tail_reg._next = uint32_t(tail_reg) + 1u;
            }
            if (pop) head_reg._next = uint32_t(head_reg) + 1u;
            if (push != pop) count_reg._next = uint32_t(count_reg) + (push ? 1u : uint32_t(-1));
        }
    }
    void _strobe() {
        turn_reg.strobe(); head_reg.strobe(); tail_reg.strobe(); count_reg.strobe(); owners_reg.strobe();
    }
};
