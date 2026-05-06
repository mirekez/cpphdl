#pragma once

#include "cpphdl.h"
#include "RAM1PORT.h"

using namespace cpphdl;

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 4, size_t WAYS = 2, int ID = 0>
class L1Cache : public Module
{
    static_assert(CACHE_LINE_SIZE == 4, "L1Cache uses 4-byte lines split into two 16-bit banks");
    static_assert(WAYS > 0, "L1Cache needs at least one way");

    static constexpr size_t SETS = TOTAL_CACHE_SIZE / CACHE_LINE_SIZE / WAYS;
    static constexpr size_t SET_BITS = clog2(SETS);
    static constexpr size_t TAG_BITS = 32 - SET_BITS - 2;
    static constexpr size_t WAY_BITS = WAYS <= 1 ? 1 : clog2(WAYS);

    static constexpr uint64_t ST_IDLE = 0;
    static constexpr uint64_t ST_LOOKUP = 1;
    static constexpr uint64_t ST_DONE = 2;
    static constexpr uint64_t ST_REFILL = 3;
    static constexpr uint64_t ST_INIT = 4;

public:
    __PORT(bool)      write_in;
    __PORT(uint32_t)  write_addr_in;
    __PORT(uint32_t)  write_data_in;
    __PORT(uint8_t)   write_mask_in;
    __PORT(bool)      read_in;
    __PORT(uint32_t)  read_addr_in;
    __PORT(uint32_t)  read_data_out = __VAR(read_data_comb_func());
    __PORT(bool)      busy_out = __VAR(busy_comb_func());
    __PORT(bool)      stall_in;

    __PORT(bool)      mem_write_out = __EXPR(write_in());
    __PORT(uint32_t)  mem_write_addr_out = __EXPR(write_addr_in());
    __PORT(uint32_t)  mem_write_data_out = __EXPR(write_data_in());
    __PORT(uint8_t)   mem_write_mask_out = __EXPR(write_mask_in());
    __PORT(bool)      mem_read_out = __VAR(mem_read_comb_func());
    __PORT(uint32_t)  mem_read_addr_out = __EXPR((uint32_t)req_addr_reg);
    __PORT(uint32_t)  mem_read_data_in;

    bool debugen_in;

private:
    RAM1PORT<16, SETS> even_ram[WAYS];
    RAM1PORT<16, SETS> odd_ram[WAYS];
    RAM1PORT<TAG_BITS + 1, SETS> even_tag_ram[WAYS];
    RAM1PORT<TAG_BITS + 1, SETS> odd_tag_ram[WAYS];

    reg<u<3>> state_reg;
    reg<u32> req_addr_reg;
    reg<u1> req_read_reg;
    reg<u<WAY_BITS>> victim_reg;
    reg<u<SET_BITS>> init_set_reg;
    reg<u32> last_addr_reg;
    reg<u32> last_data_reg;
    reg<u1> last_valid_reg;

    __LAZY_COMB(req_even_set_comb, u<SET_BITS>)
        uint32_t addr = (uint32_t)req_addr_reg;
        if (addr & 0x2) {
            addr += 2;
        }
        return req_even_set_comb = (u<SET_BITS>)(addr / 4);
    }

    __LAZY_COMB(req_odd_set_comb, u<SET_BITS>)
        uint32_t addr = (uint32_t)req_addr_reg;
        if ((addr & 0x2) == 0) {
            addr += 2;
        }
        return req_odd_set_comb = (u<SET_BITS>)(addr / 4);
    }

    __LAZY_COMB(req_even_tag_comb, u<TAG_BITS>)
        uint32_t addr = (uint32_t)req_addr_reg;
        if (addr & 0x2) {
            addr += 2;
        }
        return req_even_tag_comb = (u<TAG_BITS>)(addr >> (SET_BITS + 2));
    }

    __LAZY_COMB(req_odd_tag_comb, u<TAG_BITS>)
        uint32_t addr = (uint32_t)req_addr_reg;
        if ((addr & 0x2) == 0) {
            addr += 2;
        }
        return req_odd_tag_comb = (u<TAG_BITS>)(addr >> (SET_BITS + 2));
    }

    __LAZY_COMB(input_even_set_comb, u<SET_BITS>)
        uint32_t addr = read_in() ? read_addr_in() : write_addr_in();
        if (addr & 0x2) {
            addr += 2;
        }
        return input_even_set_comb = (u<SET_BITS>)(addr / 4);
    }

    __LAZY_COMB(input_odd_set_comb, u<SET_BITS>)
        uint32_t addr = read_in() ? read_addr_in() : write_addr_in();
        if ((addr & 0x2) == 0) {
            addr += 2;
        }
        return input_odd_set_comb = (u<SET_BITS>)(addr / 4);
    }

    __LAZY_COMB(input_even_tag_comb, logic<TAG_BITS + 1>)
        uint32_t addr = write_addr_in();
        if (addr & 0x2) {
            addr += 2;
        }
        return input_even_tag_comb = (logic<TAG_BITS + 1>)(((uint64_t)1 << TAG_BITS) | (addr >> (SET_BITS + 2)));
    }

    __LAZY_COMB(input_odd_tag_comb, logic<TAG_BITS + 1>)
        uint32_t addr = write_addr_in();
        if ((addr & 0x2) == 0) {
            addr += 2;
        }
        return input_odd_tag_comb = (logic<TAG_BITS + 1>)(((uint64_t)1 << TAG_BITS) | (addr >> (SET_BITS + 2)));
    }

    __LAZY_COMB(refill_even_tag_comb, logic<TAG_BITS + 1>)
        return refill_even_tag_comb = (logic<TAG_BITS + 1>)(((uint64_t)1 << TAG_BITS) | (uint64_t)req_even_tag_comb_func());
    }

    __LAZY_COMB(refill_odd_tag_comb, logic<TAG_BITS + 1>)
        return refill_odd_tag_comb = (logic<TAG_BITS + 1>)(((uint64_t)1 << TAG_BITS) | (uint64_t)req_odd_tag_comb_func());
    }

    __LAZY_COMB(hit_comb, bool)
        size_t i;
        hit_comb = false;
        if (state_reg == ST_LOOKUP && req_read_reg && !(req_addr_reg & 0x1)) {
            for (i = 0; i < WAYS; ++i) {
                if (even_tag_ram[i].q_out()[TAG_BITS] && odd_tag_ram[i].q_out()[TAG_BITS] &&
                    even_tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_even_tag_comb_func() &&
                    odd_tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_odd_tag_comb_func()) {
                    hit_comb = true;
                }
            }
        }
        return hit_comb;
    }

    __LAZY_COMB(cache_data_comb, uint32_t)
        size_t i;
        cache_data_comb = 0;
        for (i = 0; i < WAYS; ++i) {
            if (even_tag_ram[i].q_out()[TAG_BITS] && odd_tag_ram[i].q_out()[TAG_BITS] &&
                even_tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_even_tag_comb_func() &&
                odd_tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_odd_tag_comb_func()) {
                if (req_addr_reg & 0x2) {
                    cache_data_comb = ((uint32_t)odd_ram[i].q_out()) | ((uint32_t)even_ram[i].q_out() << 16);
                }
                else {
                    cache_data_comb = ((uint32_t)even_ram[i].q_out()) | ((uint32_t)odd_ram[i].q_out() << 16);
                }
            }
        }
        return cache_data_comb;
    }

    __LAZY_COMB(read_data_comb, uint32_t)
        if (last_valid_reg) {
            read_data_comb = last_data_reg;
        }
        else if (state_reg == ST_LOOKUP && req_read_reg && req_addr_reg == read_addr_in() && hit_comb_func()) {
            read_data_comb = cache_data_comb_func();
        }
        else {
            read_data_comb = mem_read_data_in();
        }
        return read_data_comb;
    }

    __LAZY_COMB(busy_comb, bool)
        if (state_reg == ST_INIT) {
            busy_comb = true;
        }
        else if (!read_in()) {
            busy_comb = false;
        }
        else if (last_valid_reg) {
            busy_comb = false;
        }
        else {
            busy_comb = true;
        }
        return busy_comb;
    }

    __LAZY_COMB(mem_read_comb, bool)
        return mem_read_comb = (state_reg == ST_LOOKUP || state_reg == ST_REFILL) && req_read_reg && !hit_comb_func();
    }

public:
    void _work(bool reset)
    {
        size_t i;

        if (state_reg == ST_INIT) {
            req_read_reg._next = false;
            last_valid_reg._next = false;
            if (init_set_reg == SETS - 1) {
                state_reg._next = ST_IDLE;
            }
            else {
                init_set_reg._next = init_set_reg + 1;
            }
        }
        else if (state_reg == ST_LOOKUP && req_read_reg) {
            if (hit_comb_func()) {
                last_addr_reg._next = req_addr_reg;
                last_valid_reg._next = true;
                last_data_reg._next = cache_data_comb_func();
                state_reg._next = ST_DONE;
            }
            else {
                state_reg._next = ST_REFILL;
            }
        }
        else if (state_reg == ST_REFILL && req_read_reg) {
            last_addr_reg._next = req_addr_reg;
            last_valid_reg._next = true;
            last_data_reg._next = mem_read_data_in();
            victim_reg._next = victim_reg + 1;
            state_reg._next = ST_DONE;
        }
        else if (state_reg == ST_DONE) {
            if (!stall_in()) {
                state_reg._next = ST_IDLE;
                req_read_reg._next = false;
                last_valid_reg._next = false;
            }
        }
        else if (read_in() && !last_valid_reg) {
            req_addr_reg._next = read_addr_in();
            req_read_reg._next = true;
            last_valid_reg._next = false;
            state_reg._next = ST_LOOKUP;
        }

        if (write_in()) {
            if (last_valid_reg && last_addr_reg == write_addr_in()) {
                last_valid_reg._next = false;
            }
        }

        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._work(reset);
            odd_ram[i]._work(reset);
            even_tag_ram[i]._work(reset);
            odd_tag_ram[i]._work(reset);
        }

        if (reset) {
            state_reg.clr();
            req_addr_reg.clr();
            req_read_reg.clr();
            victim_reg.clr();
            init_set_reg.clr();
            last_addr_reg.clr();
            last_data_reg.clr();
            last_valid_reg.clr();
            state_reg._next = ST_INIT;
        }
    }

    void _strobe()
    {
        state_reg.strobe();
        req_addr_reg.strobe();
        req_read_reg.strobe();
        victim_reg.strobe();
        init_set_reg.strobe();
        last_addr_reg.strobe();
        last_data_reg.strobe();
        last_valid_reg.strobe();

        size_t i;
        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._strobe();
            odd_ram[i]._strobe();
            even_tag_ram[i]._strobe();
            odd_tag_ram[i]._strobe();
        }
    }

    void _assign()
    {
        size_t i;
        for (i = 0; i < WAYS; ++i) {
            even_ram[i].addr_in = __EXPR_I((state_reg == ST_LOOKUP || state_reg == ST_REFILL) ? req_even_set_comb_func() : input_even_set_comb_func());
            even_ram[i].data_in = __EXPR_I((state_reg == ST_REFILL) ? logic<16>((req_addr_reg & 0x2) ? mem_read_data_in() >> 16 : mem_read_data_in()) : logic<16>(write_addr_in() & 0x2 ? write_data_in() >> 16 : write_data_in()));
            even_ram[i].wr_in = __EXPR_I((state_reg == ST_REFILL) && req_read_reg && victim_reg == i);
            even_ram[i].rd_in = __EXPR_I(read_in() && state_reg == ST_IDLE && !(last_valid_reg && last_addr_reg == read_addr_in()));
            even_ram[i].id_in = ID * 100 + i * 4;

            odd_ram[i].addr_in = __EXPR_I((state_reg == ST_LOOKUP || state_reg == ST_REFILL) ? req_odd_set_comb_func() : input_odd_set_comb_func());
            odd_ram[i].data_in = __EXPR_I((state_reg == ST_REFILL) ? logic<16>((req_addr_reg & 0x2) ? mem_read_data_in() : mem_read_data_in() >> 16) : logic<16>(write_addr_in() & 0x2 ? write_data_in() : write_data_in() >> 16));
            odd_ram[i].wr_in = __EXPR_I((state_reg == ST_REFILL) && req_read_reg && victim_reg == i);
            odd_ram[i].rd_in = __EXPR_I(read_in() && state_reg == ST_IDLE && !(last_valid_reg && last_addr_reg == read_addr_in()));
            odd_ram[i].id_in = ID * 100 + i * 4 + 1;

            even_tag_ram[i].addr_in = __EXPR_I((state_reg == ST_INIT) ? init_set_reg : ((state_reg == ST_LOOKUP || state_reg == ST_REFILL) ? req_even_set_comb_func() : input_even_set_comb_func()));
            even_tag_ram[i].data_in = __EXPR_I((state_reg == ST_REFILL) ? refill_even_tag_comb_func() : logic<TAG_BITS + 1>(0));
            even_tag_ram[i].wr_in = __EXPR_I((state_reg == ST_INIT) ||
                                             ((state_reg == ST_REFILL) && req_read_reg && victim_reg == i) ||
                                             write_in());
            even_tag_ram[i].rd_in = __EXPR_I(read_in() && state_reg == ST_IDLE && !(last_valid_reg && last_addr_reg == read_addr_in()));
            even_tag_ram[i].id_in = ID * 100 + i * 4 + 2;

            odd_tag_ram[i].addr_in = __EXPR_I((state_reg == ST_INIT) ? init_set_reg : ((state_reg == ST_LOOKUP || state_reg == ST_REFILL) ? req_odd_set_comb_func() : input_odd_set_comb_func()));
            odd_tag_ram[i].data_in = __EXPR_I((state_reg == ST_REFILL) ? refill_odd_tag_comb_func() : logic<TAG_BITS + 1>(0));
            odd_tag_ram[i].wr_in = __EXPR_I((state_reg == ST_INIT) ||
                                            ((state_reg == ST_REFILL) && req_read_reg && victim_reg == i) ||
                                            write_in());
            odd_tag_ram[i].rd_in = __EXPR_I(read_in() && state_reg == ST_IDLE && !(last_valid_reg && last_addr_reg == read_addr_in()));
            odd_tag_ram[i].id_in = ID * 100 + i * 4 + 3;
        }
    }
};
