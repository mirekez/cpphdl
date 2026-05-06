#pragma once

#include "cpphdl.h"
#include "RAM1PORT.h"

using namespace cpphdl;

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 2, int ID = 0, size_t ADDR_BITS = 32>
class L1Cache : public Module
{
    static_assert(CACHE_LINE_SIZE == 32, "L1Cache uses 32-byte cache lines");
    static_assert(WAYS > 0, "L1Cache needs at least one way");
    static_assert(TOTAL_CACHE_SIZE % (CACHE_LINE_SIZE * WAYS) == 0, "L1Cache geometry must divide evenly");
    static_assert(ADDR_BITS > 0 && ADDR_BITS <= 32, "L1Cache address width must be in 1..32 bits");

    static constexpr size_t LINE_WORDS = CACHE_LINE_SIZE / 4;
    static constexpr size_t SETS = TOTAL_CACHE_SIZE / CACHE_LINE_SIZE / WAYS;
    static constexpr size_t SET_BITS = clog2(SETS);
    static constexpr size_t WORD_BITS = clog2(LINE_WORDS);
    static constexpr size_t LINE_BITS = clog2(CACHE_LINE_SIZE);
    static constexpr size_t HALF_LINE_BITS = CACHE_LINE_SIZE * 4;
    static constexpr size_t TAG_BITS = ADDR_BITS - SET_BITS - LINE_BITS;
    static constexpr size_t WAY_BITS = WAYS <= 1 ? 1 : clog2(WAYS);
    static constexpr bool FAST_LOOKUP_HIT = ID == 0;
    static constexpr bool STREAM_HITS = false;
    static_assert(ADDR_BITS > SET_BITS + LINE_BITS, "L1Cache address width must include tag bits");

    static constexpr uint64_t ST_IDLE = 0;
    static constexpr uint64_t ST_LOOKUP = 1;
    static constexpr uint64_t ST_DONE = 2;
    static constexpr uint64_t ST_REFILL = 3;
    static constexpr uint64_t ST_INIT = 4;

public:
    __PORT(bool)      write_in;
    __PORT(uint32_t)  write_data_in;
    __PORT(uint8_t)   write_mask_in;
    __PORT(bool)      read_in;
    __PORT(uint32_t)  addr_in;
    __PORT(uint32_t)  read_data_out = __VAR(read_data_comb_func());
    __PORT(uint32_t)  read_addr_out = __VAR(read_addr_comb_func());
    __PORT(bool)      read_valid_out = __VAR(read_valid_comb_func());
    __PORT(bool)      busy_out = __VAR(busy_comb_func());
    __PORT(bool)      stall_in;
    __PORT(bool)      flush_in;
    __PORT(bool)      fast_in;

    __PORT(bool)      mem_write_out = __EXPR(write_in());
    __PORT(uint32_t)  mem_write_data_out = __EXPR(write_data_in());
    __PORT(uint8_t)   mem_write_mask_out = __EXPR(write_mask_in());
    __PORT(bool)      mem_read_out = __VAR(mem_read_comb_func());
    __PORT(uint32_t)  mem_addr_out = __VAR(mem_addr_comb_func());
    __PORT(uint32_t)  mem_read_data_in;
    __PORT(u<3>)      perf_state_out = __VAR(perf_state_comb_func());
    __PORT(bool)      perf_hit_out = __VAR(hit_comb_func());
    __PORT(bool)      perf_lookup_wait_out = __VAR(perf_lookup_wait_comb_func());
    __PORT(bool)      perf_refill_wait_out = __VAR(perf_refill_wait_comb_func());
    __PORT(bool)      perf_init_wait_out = __VAR(perf_init_wait_comb_func());
    __PORT(bool)      perf_issue_wait_out = __VAR(perf_issue_wait_comb_func());

    bool debugen_in;

private:
    RAM1PORT<HALF_LINE_BITS, SETS> even_ram[WAYS];
    RAM1PORT<HALF_LINE_BITS, SETS> odd_ram[WAYS];
    RAM1PORT<TAG_BITS + 1, SETS> tag_ram[WAYS];

    reg<u<3>> state_reg;
    reg<u32> req_addr_reg;
    reg<u1> req_read_reg;
    reg<u1> req_cacheable_reg;
    reg<u<WORD_BITS>> refill_word_reg;
    reg<u<WAY_BITS>> victim_reg;
    reg<u<SET_BITS>> init_set_reg;
    reg<u32> last_addr_reg;
    reg<u32> last_data_reg;
    reg<u1> last_valid_reg;
    reg<u32> pipe_addr_reg;
    reg<u1> pipe_valid_reg;
    reg<u1> pipe_cacheable_reg;
    reg<u<SET_BITS>> recent_set_reg;
    reg<u<TAG_BITS>> recent_tag_reg;
    reg<u1> recent_valid_reg;
    reg<logic<HALF_LINE_BITS>> recent_even_line_reg;
    reg<logic<HALF_LINE_BITS>> recent_odd_line_reg;
    reg<u16> refill_low_half_reg;
    reg<logic<HALF_LINE_BITS>> refill_even_line_reg;
    reg<logic<HALF_LINE_BITS>> refill_odd_line_reg;
    __LAZY_COMB(req_set_comb, u<SET_BITS>)
        return req_set_comb = (u<SET_BITS>)((uint32_t)req_addr_reg >> LINE_BITS);
    }

    __LAZY_COMB(req_tag_comb, u<TAG_BITS>)
        return req_tag_comb = (u<TAG_BITS>)((uint32_t)req_addr_reg >> (LINE_BITS + SET_BITS));
    }

    __LAZY_COMB(req_word_comb, u<WORD_BITS>)
        return req_word_comb = (u<WORD_BITS>)(((uint32_t)req_addr_reg >> 2) & (LINE_WORDS - 1));
    }

    __LAZY_COMB(req_cacheable_comb, bool)
        uint32_t word = ((uint32_t)req_addr_reg >> 2) & (LINE_WORDS - 1);
        req_cacheable_comb = !(req_addr_reg & 0x1);
        if ((req_addr_reg & 0x2) && word == LINE_WORDS - 1) {
            req_cacheable_comb = false;
        }
        return req_cacheable_comb;
    }

    __LAZY_COMB(input_set_comb, u<SET_BITS>)
        return input_set_comb = (u<SET_BITS>)(addr_in() >> LINE_BITS);
    }

    __LAZY_COMB(input_tag_comb, u<TAG_BITS>)
        return input_tag_comb = (u<TAG_BITS>)(addr_in() >> (LINE_BITS + SET_BITS));
    }

    __LAZY_COMB(input_word_comb, u<WORD_BITS>)
        return input_word_comb = (u<WORD_BITS>)((addr_in() >> 2) & (LINE_WORDS - 1));
    }

    __LAZY_COMB(input_cacheable_comb, bool)
        uint32_t word = (addr_in() >> 2) & (LINE_WORDS - 1);
        input_cacheable_comb = !(addr_in() & 0x1);
        if ((addr_in() & 0x2) && word == LINE_WORDS - 1) {
            input_cacheable_comb = false;
        }
        return input_cacheable_comb;
    }

    __LAZY_COMB(pipe_tag_comb, u<TAG_BITS>)
        return pipe_tag_comb = (u<TAG_BITS>)(pipe_addr_reg >> (LINE_BITS + SET_BITS));
    }

    __LAZY_COMB(pipe_word_comb, u<WORD_BITS>)
        return pipe_word_comb = (u<WORD_BITS>)(((uint32_t)pipe_addr_reg >> 2) & (LINE_WORDS - 1));
    }

    __LAZY_COMB(input_recent_hit_comb, bool)
        input_recent_hit_comb = false;
        if (FAST_LOOKUP_HIT && fast_in() && recent_valid_reg && input_cacheable_comb_func() &&
            recent_set_reg == input_set_comb_func() &&
            recent_tag_reg == input_tag_comb_func()) {
            input_recent_hit_comb = true;
        }
        return input_recent_hit_comb;
    }

    __LAZY_COMB(refill_tag_comb, logic<TAG_BITS + 1>)
        return refill_tag_comb = (logic<TAG_BITS + 1>)(((uint64_t)1 << TAG_BITS) | (uint64_t)req_tag_comb_func());
    }

    __LAZY_COMB(refill_even_line_comb, logic<HALF_LINE_BITS>)
        uint32_t word = (uint32_t)refill_word_reg;
        refill_even_line_comb = refill_even_line_reg;
        refill_even_line_comb.bits(word * 16 + 15, word * 16) = mem_read_data_in() & 0xFFFF;
        return refill_even_line_comb;
    }

    __LAZY_COMB(refill_odd_line_comb, logic<HALF_LINE_BITS>)
        uint32_t word = (uint32_t)refill_word_reg;
        refill_odd_line_comb = refill_odd_line_reg;
        refill_odd_line_comb.bits(word * 16 + 15, word * 16) = mem_read_data_in() >> 16;
        return refill_odd_line_comb;
    }

    __LAZY_COMB(hit_comb, bool)
        size_t i;
        hit_comb = false;
        if (state_reg == ST_LOOKUP && req_read_reg && req_cacheable_reg) {
            for (i = 0; i < WAYS; ++i) {
                if (tag_ram[i].q_out()[TAG_BITS] &&
                    tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                    hit_comb = true;
                }
            }
        }
        return hit_comb;
    }

    __LAZY_COMB(stream_hit_comb, bool)
        size_t i;
        stream_hit_comb = false;
        if (STREAM_HITS && state_reg == ST_IDLE && pipe_valid_reg && pipe_cacheable_reg) {
            if (recent_valid_reg && recent_set_reg == (u<SET_BITS>)(pipe_addr_reg >> LINE_BITS) &&
                recent_tag_reg == pipe_tag_comb_func()) {
                stream_hit_comb = true;
            }
            for (i = 0; i < WAYS; ++i) {
                if (tag_ram[i].q_out()[TAG_BITS] &&
                    tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == pipe_tag_comb_func()) {
                    stream_hit_comb = true;
                }
            }
        }
        return stream_hit_comb;
    }

    __LAZY_COMB(stream_miss_comb, bool)
        return stream_miss_comb = STREAM_HITS && state_reg == ST_IDLE && pipe_valid_reg &&
            (!pipe_cacheable_reg || !stream_hit_comb_func());
    }

    __LAZY_COMB(cache_data_comb, uint32_t)
        size_t i;
        uint32_t word;
        uint32_t even_half;
        uint32_t odd_half;
        cache_data_comb = 0;
        word = (uint32_t)req_word_comb_func();
        even_half = 0;
        odd_half = 0;
        for (i = 0; i < WAYS; ++i) {
            if (tag_ram[i].q_out()[TAG_BITS] &&
                tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                even_half = (uint32_t)even_ram[i].q_out().bits(word * 16 + 15, word * 16);
                odd_half = (uint32_t)odd_ram[i].q_out().bits(word * 16 + 15, word * 16);
                if (req_addr_reg & 0x2) {
                    even_half = (uint32_t)even_ram[i].q_out().bits((word + 1) * 16 + 15, (word + 1) * 16);
                    cache_data_comb = odd_half | (even_half << 16);
                }
                else {
                    cache_data_comb = even_half | (odd_half << 16);
                }
            }
        }
        return cache_data_comb;
    }

    __LAZY_COMB(hit_even_line_comb, logic<HALF_LINE_BITS>)
        size_t i;
        hit_even_line_comb = 0;
        for (i = 0; i < WAYS; ++i) {
            if (tag_ram[i].q_out()[TAG_BITS] &&
                tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                hit_even_line_comb = even_ram[i].q_out();
            }
        }
        return hit_even_line_comb;
    }

    __LAZY_COMB(hit_odd_line_comb, logic<HALF_LINE_BITS>)
        size_t i;
        hit_odd_line_comb = 0;
        for (i = 0; i < WAYS; ++i) {
            if (tag_ram[i].q_out()[TAG_BITS] &&
                tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                hit_odd_line_comb = odd_ram[i].q_out();
            }
        }
        return hit_odd_line_comb;
    }

    __LAZY_COMB(input_recent_data_comb, uint32_t)
        uint32_t word;
        uint32_t even_half;
        uint32_t odd_half;
        word = (addr_in() >> 2) & (LINE_WORDS - 1);
        even_half = (uint32_t)recent_even_line_reg.bits(word * 16 + 15, word * 16);
        odd_half = (uint32_t)recent_odd_line_reg.bits(word * 16 + 15, word * 16);
        if (addr_in() & 0x2) {
            even_half = (uint32_t)recent_even_line_reg.bits((word + 1) * 16 + 15, (word + 1) * 16);
            input_recent_data_comb = odd_half | (even_half << 16);
        }
        else {
            input_recent_data_comb = even_half | (odd_half << 16);
        }
        return input_recent_data_comb;
    }

    __LAZY_COMB(stream_data_comb, uint32_t)
        size_t i;
        uint32_t word;
        uint32_t even_half;
        uint32_t odd_half;
        stream_data_comb = 0;
        word = (uint32_t)pipe_word_comb_func();
        even_half = 0;
        odd_half = 0;
        if (recent_valid_reg && recent_set_reg == (u<SET_BITS>)(pipe_addr_reg >> LINE_BITS) &&
            recent_tag_reg == pipe_tag_comb_func()) {
            even_half = (uint32_t)recent_even_line_reg.bits(word * 16 + 15, word * 16);
            odd_half = (uint32_t)recent_odd_line_reg.bits(word * 16 + 15, word * 16);
            if (pipe_addr_reg & 0x2) {
                even_half = (uint32_t)recent_even_line_reg.bits((word + 1) * 16 + 15, (word + 1) * 16);
                stream_data_comb = odd_half | (even_half << 16);
            }
            else {
                stream_data_comb = even_half | (odd_half << 16);
            }
        }
        for (i = 0; i < WAYS; ++i) {
            if (tag_ram[i].q_out()[TAG_BITS] &&
                tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == pipe_tag_comb_func()) {
                even_half = (uint32_t)even_ram[i].q_out().bits(word * 16 + 15, word * 16);
                odd_half = (uint32_t)odd_ram[i].q_out().bits(word * 16 + 15, word * 16);
                if (pipe_addr_reg & 0x2) {
                    even_half = (uint32_t)even_ram[i].q_out().bits((word + 1) * 16 + 15, (word + 1) * 16);
                    stream_data_comb = odd_half | (even_half << 16);
                }
                else {
                    stream_data_comb = even_half | (odd_half << 16);
                }
            }
        }
        return stream_data_comb;
    }

    __LAZY_COMB(read_data_comb, uint32_t)
        if (state_reg == ST_IDLE && input_recent_hit_comb_func()) {
            read_data_comb = input_recent_data_comb_func();
        }
        else if (STREAM_HITS && state_reg == ST_IDLE && stream_hit_comb_func()) {
            read_data_comb = stream_data_comb_func();
        }
        else if (last_valid_reg) {
            read_data_comb = last_data_reg;
        }
        else if (state_reg == ST_LOOKUP && req_read_reg && req_addr_reg == addr_in() && hit_comb_func()) {
            read_data_comb = cache_data_comb_func();
        }
        else {
            read_data_comb = mem_read_data_in();
        }
        return read_data_comb;
    }

    __LAZY_COMB(read_addr_comb, uint32_t)
        if (STREAM_HITS && state_reg == ST_IDLE && stream_hit_comb_func()) {
            read_addr_comb = pipe_addr_reg;
        }
        else if (last_valid_reg) {
            read_addr_comb = last_addr_reg;
        }
        else if (STREAM_HITS) {
            read_addr_comb = pipe_addr_reg;
        }
        else {
            read_addr_comb = req_addr_reg;
        }
        return read_addr_comb;
    }

    __LAZY_COMB(read_valid_comb, bool)
        if (STREAM_HITS) {
            read_valid_comb = (state_reg == ST_IDLE && stream_hit_comb_func()) || last_valid_reg;
        }
        else {
            read_valid_comb = last_valid_reg ||
                (FAST_LOOKUP_HIT && state_reg == ST_LOOKUP && req_read_reg && hit_comb_func());
        }
        return read_valid_comb;
    }

    __LAZY_COMB(busy_comb, bool)
        if (state_reg == ST_INIT) {
            busy_comb = true;
        }
        else if (last_valid_reg) {
            busy_comb = false;
        }
        else if (stream_miss_comb_func()) {
            busy_comb = true;
        }
        else if (FAST_LOOKUP_HIT && state_reg == ST_LOOKUP && req_read_reg && hit_comb_func()) {
            busy_comb = false;
        }
        else if (state_reg != ST_IDLE) {
            busy_comb = true;
        }
        else if (read_in() && input_recent_hit_comb_func()) {
            busy_comb = false;
        }
        else if (read_in()) {
            busy_comb = STREAM_HITS ? false : true;
        }
        else {
            busy_comb = false;
        }
        return busy_comb;
    }

    __LAZY_COMB(perf_state_comb, u<3>)
        return perf_state_comb = state_reg;
    }

    __LAZY_COMB(perf_lookup_wait_comb, bool)
        return perf_lookup_wait_comb = busy_comb_func() && state_reg == ST_LOOKUP;
    }

    __LAZY_COMB(perf_refill_wait_comb, bool)
        return perf_refill_wait_comb = busy_comb_func() && state_reg == ST_REFILL;
    }

    __LAZY_COMB(perf_init_wait_comb, bool)
        return perf_init_wait_comb = busy_comb_func() && state_reg == ST_INIT;
    }

    __LAZY_COMB(perf_issue_wait_comb, bool)
        return perf_issue_wait_comb = busy_comb_func() && state_reg == ST_IDLE && read_in() &&
            !input_recent_hit_comb_func() && !stream_miss_comb_func();
    }

    __LAZY_COMB(mem_read_comb, bool)
        return mem_read_comb = state_reg == ST_REFILL && req_read_reg;
    }

    __LAZY_COMB(mem_addr_comb, uint32_t)
        if (state_reg == ST_REFILL && req_read_reg && req_cacheable_reg) {
            mem_addr_comb = ((uint32_t)req_addr_reg & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + ((uint32_t)refill_word_reg * 4);
        }
        else if (state_reg == ST_REFILL && req_read_reg) {
            mem_addr_comb = req_addr_reg;
        }
        else {
            mem_addr_comb = addr_in();
        }
        return mem_addr_comb;
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
        else if (stream_miss_comb_func()) {
            req_addr_reg._next = pipe_addr_reg;
            req_read_reg._next = true;
            req_cacheable_reg._next = pipe_cacheable_reg;
            refill_word_reg._next = 0;
            refill_low_half_reg._next = 0;
            refill_even_line_reg._next = 0;
            refill_odd_line_reg._next = 0;
            pipe_valid_reg._next = false;
            state_reg._next = ST_REFILL;
        }
        else if (state_reg == ST_LOOKUP && req_read_reg) {
            if (hit_comb_func()) {
                recent_set_reg._next = req_set_comb_func();
                recent_tag_reg._next = req_tag_comb_func();
                recent_valid_reg._next = true;
                recent_even_line_reg._next = hit_even_line_comb_func();
                recent_odd_line_reg._next = hit_odd_line_comb_func();
                if (FAST_LOOKUP_HIT && !stall_in()) {
                    req_read_reg._next = false;
                    req_cacheable_reg._next = false;
                    last_valid_reg._next = false;
                    state_reg._next = ST_IDLE;
                }
                else {
                    last_addr_reg._next = req_addr_reg;
                    last_valid_reg._next = true;
                    last_data_reg._next = cache_data_comb_func();
                    state_reg._next = ST_DONE;
                }
            }
            else {
                refill_word_reg._next = 0;
                refill_low_half_reg._next = 0;
                refill_even_line_reg._next = 0;
                refill_odd_line_reg._next = 0;
                state_reg._next = ST_REFILL;
            }
        }
        else if (state_reg == ST_REFILL && req_read_reg) {
            if (req_cacheable_reg) {
                refill_even_line_reg._next = refill_even_line_comb_func();
                refill_odd_line_reg._next = refill_odd_line_comb_func();
                if ((req_addr_reg & 0x2) == 0 && refill_word_reg == req_word_comb_func()) {
                    last_data_reg._next = mem_read_data_in();
                }
                if ((req_addr_reg & 0x2) && refill_word_reg == req_word_comb_func()) {
                    refill_low_half_reg._next = (u16)(mem_read_data_in() >> 16);
                }
                if ((req_addr_reg & 0x2) && refill_word_reg == req_word_comb_func() + 1) {
                    last_data_reg._next = (uint32_t)refill_low_half_reg | ((uint32_t)(mem_read_data_in() & 0xFFFF) << 16);
                }
                if (refill_word_reg == LINE_WORDS - 1) {
                    last_addr_reg._next = req_addr_reg;
                    last_valid_reg._next = true;
                    recent_set_reg._next = req_set_comb_func();
                    recent_tag_reg._next = req_tag_comb_func();
                    recent_valid_reg._next = true;
                    recent_even_line_reg._next = refill_even_line_comb_func();
                    recent_odd_line_reg._next = refill_odd_line_comb_func();
                    victim_reg._next = victim_reg + 1;
                    state_reg._next = ST_DONE;
                }
                else {
                    refill_word_reg._next = refill_word_reg + 1;
                }
            }
            else {
                last_addr_reg._next = req_addr_reg;
                last_valid_reg._next = true;
                last_data_reg._next = mem_read_data_in();
                state_reg._next = ST_DONE;
            }
        }
        else if (state_reg == ST_DONE) {
            if (!stall_in()) {
                state_reg._next = ST_IDLE;
                req_read_reg._next = false;
                req_cacheable_reg._next = false;
                last_valid_reg._next = false;
            }
        }
        else if (read_in() && !last_valid_reg) {
            if (input_recent_hit_comb_func()) {
                req_read_reg._next = false;
                req_cacheable_reg._next = false;
                state_reg._next = ST_IDLE;
            }
            else if (STREAM_HITS) {
                if (!stall_in()) {
                    pipe_addr_reg._next = addr_in();
                    pipe_valid_reg._next = true;
                    pipe_cacheable_reg._next = input_cacheable_comb_func();
                }
            }
            else {
                req_addr_reg._next = addr_in();
                req_read_reg._next = true;
                req_cacheable_reg._next = input_cacheable_comb_func();
                last_valid_reg._next = false;
                state_reg._next = ST_LOOKUP;
            }
        }

        if (STREAM_HITS && flush_in() && state_reg == ST_IDLE) {
            last_valid_reg._next = false;
            pipe_addr_reg._next = addr_in();
            pipe_valid_reg._next = read_in();
            pipe_cacheable_reg._next = input_cacheable_comb_func();
        }

        if (write_in()) {
            if (last_valid_reg && last_addr_reg == addr_in()) {
                last_valid_reg._next = false;
            }
            recent_valid_reg._next = false;
        }

        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._work(reset);
            odd_ram[i]._work(reset);
            tag_ram[i]._work(reset);
        }

        if (reset) {
            state_reg.clr();
            req_addr_reg.clr();
            req_read_reg.clr();
            req_cacheable_reg.clr();
            refill_word_reg.clr();
            victim_reg.clr();
            init_set_reg.clr();
            last_addr_reg.clr();
            last_data_reg.clr();
            last_valid_reg.clr();
            pipe_addr_reg.clr();
            pipe_valid_reg.clr();
            pipe_cacheable_reg.clr();
            recent_set_reg.clr();
            recent_tag_reg.clr();
            recent_valid_reg.clr();
            recent_even_line_reg.clr();
            recent_odd_line_reg.clr();
            refill_low_half_reg.clr();
            refill_even_line_reg.clr();
            refill_odd_line_reg.clr();
            state_reg._next = ST_INIT;
        }
    }

    void _strobe()
    {
        state_reg.strobe();
        req_addr_reg.strobe();
        req_read_reg.strobe();
        req_cacheable_reg.strobe();
        refill_word_reg.strobe();
        victim_reg.strobe();
        init_set_reg.strobe();
        last_addr_reg.strobe();
        last_data_reg.strobe();
        last_valid_reg.strobe();
        pipe_addr_reg.strobe();
        pipe_valid_reg.strobe();
        pipe_cacheable_reg.strobe();
        recent_set_reg.strobe();
        recent_tag_reg.strobe();
        recent_valid_reg.strobe();
        recent_even_line_reg.strobe();
        recent_odd_line_reg.strobe();
        refill_low_half_reg.strobe();
        refill_even_line_reg.strobe();
        refill_odd_line_reg.strobe();
        size_t i;
        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._strobe();
            odd_ram[i]._strobe();
            tag_ram[i]._strobe();
        }
    }

    void _assign()
    {
        size_t i;
        for (i = 0; i < WAYS; ++i) {
            even_ram[i].addr_in = __EXPR((state_reg == ST_LOOKUP || state_reg == ST_REFILL) ? req_set_comb_func() : input_set_comb_func());
            even_ram[i].data_in = __EXPR(refill_even_line_comb_func());
            even_ram[i].wr_in = __EXPR_I((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_word_reg == LINE_WORDS - 1 && victim_reg == i);
            even_ram[i].rd_in = __EXPR(read_in() && state_reg == ST_IDLE && input_cacheable_comb_func() && !input_recent_hit_comb_func() && !stream_miss_comb_func() && !(last_valid_reg && last_addr_reg == addr_in()));
            even_ram[i].id_in = ID * 100 + i * 3;

            odd_ram[i].addr_in = __EXPR((state_reg == ST_LOOKUP || state_reg == ST_REFILL) ? req_set_comb_func() : input_set_comb_func());
            odd_ram[i].data_in = __EXPR(refill_odd_line_comb_func());
            odd_ram[i].wr_in = __EXPR_I((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_word_reg == LINE_WORDS - 1 && victim_reg == i);
            odd_ram[i].rd_in = __EXPR(read_in() && state_reg == ST_IDLE && input_cacheable_comb_func() && !input_recent_hit_comb_func() && !stream_miss_comb_func() && !(last_valid_reg && last_addr_reg == addr_in()));
            odd_ram[i].id_in = ID * 100 + i * 3 + 1;

            tag_ram[i].addr_in = __EXPR((state_reg == ST_INIT) ? init_set_reg : ((state_reg == ST_LOOKUP || state_reg == ST_REFILL) ? req_set_comb_func() : input_set_comb_func()));
            tag_ram[i].data_in = __EXPR((state_reg == ST_REFILL) ? refill_tag_comb_func() : logic<TAG_BITS + 1>(0));
            tag_ram[i].wr_in = __EXPR_I((state_reg == ST_INIT) ||
                                        ((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_word_reg == LINE_WORDS - 1 && victim_reg == i) ||
                                        write_in());
            tag_ram[i].rd_in = __EXPR(read_in() && state_reg == ST_IDLE && input_cacheable_comb_func() && !input_recent_hit_comb_func() && !stream_miss_comb_func() && !(last_valid_reg && last_addr_reg == addr_in()));
            tag_ram[i].id_in = ID * 100 + i * 3 + 2;
        }
    }
};
