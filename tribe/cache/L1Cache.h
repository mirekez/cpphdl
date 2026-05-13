#pragma once

#include "cpphdl.h"
#include "RAM1PORT.h"

using namespace cpphdl;

struct L1CachePerf
{
    unsigned hit:1;
    unsigned lookup_wait:1;
    unsigned refill_wait:1;
    unsigned init_wait:1;
    unsigned issue_wait:1;
    u<3> state;
} __PACKED;

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 2, int ID = 0, size_t ADDR_BITS = 32, size_t PORT_BITWIDTH = 32>
class L1Cache : public Module
{
    static_assert(CACHE_LINE_SIZE == 32, "L1Cache uses 32-byte cache lines");
    static_assert(PORT_BITWIDTH >= 32 && PORT_BITWIDTH % 32 == 0, "L1Cache refill port must be a whole number of 32-bit words");
    static_assert((CACHE_LINE_SIZE * 8) % PORT_BITWIDTH == 0, "L1Cache refill port must divide a cache line");
    static_assert(WAYS > 0, "L1Cache needs at least one way");
    static_assert(TOTAL_CACHE_SIZE % (CACHE_LINE_SIZE * WAYS) == 0, "L1Cache geometry must divide evenly");
    static_assert(ADDR_BITS > 0 && ADDR_BITS <= 32, "L1Cache address width must be in 1..32 bits");

    static constexpr size_t LINE_WORDS = CACHE_LINE_SIZE / 4;
    static constexpr size_t SETS = TOTAL_CACHE_SIZE / CACHE_LINE_SIZE / WAYS;
    static constexpr size_t SET_BITS = clog2(SETS);
    static constexpr size_t WORD_BITS = clog2(LINE_WORDS);
    static constexpr size_t LINE_BITS = clog2(CACHE_LINE_SIZE);
    static constexpr size_t HALF_LINE_BITS = CACHE_LINE_SIZE * 4;
    static constexpr size_t PORT_BYTES = PORT_BITWIDTH / 8;
    static constexpr size_t PORT_WORDS = PORT_BITWIDTH / 32;
    static constexpr size_t REFILL_BEATS = CACHE_LINE_SIZE / PORT_BYTES;
    static constexpr size_t REFILL_BEAT_BITS = REFILL_BEATS <= 1 ? 1 : clog2(REFILL_BEATS);
    static constexpr size_t TAG_BITS = ADDR_BITS - SET_BITS - LINE_BITS;
    static constexpr size_t WAY_BITS = WAYS <= 1 ? 1 : clog2(WAYS);
    static_assert(ADDR_BITS > SET_BITS + LINE_BITS, "L1Cache address width must include tag bits");

    static constexpr uint64_t ST_IDLE = 0;    // No active cache request; a new CPU read can be accepted.
    static constexpr uint64_t ST_LOOKUP = 1;  // Tag/data RAM outputs belong to the accepted request; hit/miss is resolved here.
    static constexpr uint64_t ST_DONE = 2;    // Read data is ready but held until downstream logic can accept it.
    static constexpr uint64_t ST_REFILL = 3;  // Cache miss path; fetch one 32-byte line from backing memory.
    static constexpr uint64_t ST_INIT = 4;    // Startup clear of tag valid bits, one set per clock.

public:
    _PORT(bool)      write_in;
    _PORT(uint32_t)  write_data_in;
    _PORT(uint8_t)   write_mask_in;
    _PORT(bool)      read_in;
    _PORT(uint32_t)  addr_in;
    _PORT(uint32_t)  read_data_out = _ASSIGN_COMB(read_data_comb_func());
    _PORT(uint32_t)  read_addr_out = _ASSIGN_COMB(read_addr_comb_func());
    _PORT(bool)      read_valid_out = _ASSIGN_COMB(read_valid_comb_func());
    _PORT(bool)      busy_out = _ASSIGN_COMB(busy_comb_func());
    _PORT(bool)      stall_in;
    _PORT(bool)      flush_in;
    _PORT(bool)      invalidate_in;
    _PORT(bool)      cache_disable_in;

    _PORT(bool)      mem_write_out = _ASSIGN(write_in());
    _PORT(uint32_t)  mem_write_data_out = _ASSIGN(write_data_in());
    _PORT(uint8_t)   mem_write_mask_out = _ASSIGN(write_mask_in());
    _PORT(bool)      mem_read_out = _ASSIGN_COMB(mem_read_comb_func());
    _PORT(uint32_t)  mem_addr_out = _ASSIGN_COMB(mem_addr_comb_func());
    _PORT(logic<PORT_BITWIDTH>) mem_read_data_in;
    _PORT(bool)      mem_wait_in;
    _PORT(L1CachePerf) perf_out = _ASSIGN_COMB(perf_comb_func());

    bool debugen_in;

    void _assign()
    {
        size_t i;
        for (i = 0; i < WAYS; ++i) {
            even_ram[i].addr_in = _ASSIGN((state_reg == ST_REFILL || (state_reg == ST_LOOKUP && !issue_read_comb_func())) ? req_set_comb_func() : input_set_comb_func());
            even_ram[i].data_in = _ASSIGN(refill_even_line_comb_func());
            even_ram[i].wr_in = _ASSIGN_I((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_beat_reg == REFILL_BEATS - 1 && victim_reg == i);
            even_ram[i].rd_in = _ASSIGN(issue_read_comb_func() && input_cacheable_comb_func());
            even_ram[i].id_in = ID * 100 + i * 3;

            odd_ram[i].addr_in = _ASSIGN((state_reg == ST_REFILL || (state_reg == ST_LOOKUP && !issue_read_comb_func())) ? req_set_comb_func() : input_set_comb_func());
            odd_ram[i].data_in = _ASSIGN(refill_odd_line_comb_func());
            odd_ram[i].wr_in = _ASSIGN_I((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_beat_reg == REFILL_BEATS - 1 && victim_reg == i);
            odd_ram[i].rd_in = _ASSIGN(issue_read_comb_func() && input_cacheable_comb_func());
            odd_ram[i].id_in = ID * 100 + i * 3 + 1;

            tag_ram[i].addr_in = _ASSIGN((state_reg == ST_INIT) ? init_set_reg :
                                        (write_in() ? input_set_comb_func() :
                                         ((state_reg == ST_REFILL || (state_reg == ST_LOOKUP && !issue_read_comb_func())) ? req_set_comb_func() : input_set_comb_func())));
            tag_ram[i].data_in = _ASSIGN((state_reg == ST_REFILL) ? refill_tag_comb_func() : logic<TAG_BITS + 1>(0));
            tag_ram[i].wr_in = _ASSIGN_I((state_reg == ST_INIT) ||
                                        ((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_beat_reg == REFILL_BEATS - 1 && victim_reg == i) ||
                                        write_in());
            tag_ram[i].rd_in = _ASSIGN(issue_read_comb_func() && input_cacheable_comb_func());
            tag_ram[i].id_in = ID * 100 + i * 3 + 2;
        }
    }

private:
    RAM1PORT<HALF_LINE_BITS, SETS> even_ram[WAYS];
    RAM1PORT<HALF_LINE_BITS, SETS> odd_ram[WAYS];
    RAM1PORT<TAG_BITS + 1, SETS> tag_ram[WAYS];

    reg<u<3>> state_reg;
    reg<u32> req_addr_reg;
    reg<u1> req_read_reg;
    reg<u1> req_cacheable_reg;
    reg<u1> req_cache_disable_reg;
    reg<u<REFILL_BEAT_BITS>> refill_beat_reg;
    reg<u<WAY_BITS>> victim_reg;
    reg<u<SET_BITS>> init_set_reg;
    reg<u32> last_addr_reg;
    reg<u32> last_data_reg;
    reg<u1> last_valid_reg;
    reg<logic<HALF_LINE_BITS>> refill_even_line_reg;
    reg<logic<HALF_LINE_BITS>> refill_odd_line_reg;

    // Set index of the registered request address.
    _LAZY_COMB(req_set_comb, u<SET_BITS>)
        return req_set_comb = (u<SET_BITS>)((uint32_t)req_addr_reg >> LINE_BITS);
    }

    // Tag bits of the registered request address.
    _LAZY_COMB(req_tag_comb, u<TAG_BITS>)
        return req_tag_comb = (u<TAG_BITS>)((uint32_t)req_addr_reg >> (LINE_BITS + SET_BITS));
    }

    // 32-bit word index inside the registered cache line.
    _LAZY_COMB(req_word_comb, u<WORD_BITS>)
        return req_word_comb = (u<WORD_BITS>)(((uint32_t)req_addr_reg >> 2) & (LINE_WORDS - 1));
    }

    // Set index of the incoming CPU address.
    _LAZY_COMB(input_set_comb, u<SET_BITS>)
        return input_set_comb = (u<SET_BITS>)(addr_in() >> LINE_BITS);
    }

    // Whether the incoming address can be served from this cache line format.
    _LAZY_COMB(input_cacheable_comb, bool)
        input_cacheable_comb = !cache_disable_in() && !(addr_in() & 0x1);
        if (ID != 0 && (addr_in() & 0x3u) != 0 &&
            (((addr_in() >> 2) & (LINE_WORDS - 1)) == LINE_WORDS - 1)) {
            // A 32-bit read starting at the final line word needs bytes from the next line.
            // Let L2's cross-line path assemble it instead of filling an incomplete L1 line.
            input_cacheable_comb = false;
        }
        return input_cacheable_comb;
    }

    // True when a new CPU read should be issued into tag/data RAMs.
    _LAZY_COMB(start_read_comb, bool)
        start_read_comb = false;
        if (read_in() && !stall_in()) {
            if (state_reg == ST_IDLE) {
                start_read_comb = true;
            }
            if (state_reg == ST_DONE && (addr_in() != (uint32_t)last_addr_reg || !req_cacheable_reg)) {
                start_read_comb = true;
            }
            if (state_reg == ST_LOOKUP && req_read_reg && hit_comb_func() && addr_in() != (uint32_t)req_addr_reg) {
                start_read_comb = true;
            }
        }
        return start_read_comb;
    }

    // Redirects must issue a RAM read even while the old branch instruction is stalling fetch.
    _LAZY_COMB(issue_read_comb, bool)
        return issue_read_comb = (flush_in() && read_in()) || start_read_comb_func();
    }

    // Tag RAM payload written at the end of a refill: valid bit plus tag.
    _LAZY_COMB(refill_tag_comb, logic<TAG_BITS + 1>)
        return refill_tag_comb = (logic<TAG_BITS + 1>)(((uint64_t)1 << TAG_BITS) | (uint64_t)req_tag_comb_func());
    }

    // Next even-half line image while streaming refill words from memory.
    _LAZY_COMB(refill_even_line_comb, logic<HALF_LINE_BITS>)
        size_t i;
        uint32_t word;
        refill_even_line_comb = refill_even_line_reg;
        for (i = 0; i < PORT_WORDS; ++i) {
            word = (uint32_t)refill_beat_reg * PORT_WORDS + i;
            // Store bits [15:0] from each 32-bit refill word in the even half RAM.
            refill_even_line_comb.bits(word * 16 + 15, word * 16) =
                (uint32_t)mem_read_data_in().bits(i * 32 + 15, i * 32);
        }
        return refill_even_line_comb;
    }

    // Next odd-half line image while streaming refill words from memory.
    _LAZY_COMB(refill_odd_line_comb, logic<HALF_LINE_BITS>)
        size_t i;
        uint32_t word;
        refill_odd_line_comb = refill_odd_line_reg;
        for (i = 0; i < PORT_WORDS; ++i) {
            word = (uint32_t)refill_beat_reg * PORT_WORDS + i;
            // Store bits [31:16] from each 32-bit refill word in the odd half RAM.
            refill_odd_line_comb.bits(word * 16 + 15, word * 16) =
                (uint32_t)mem_read_data_in().bits(i * 32 + 31, i * 32 + 16);
        }
        return refill_odd_line_comb;
    }

    // Requested word assembled from the completed refill line image.
    _LAZY_COMB(refill_data_comb, uint32_t)
        uint32_t word;
        uint32_t even_half;
        uint32_t odd_half;
        word = (uint32_t)req_word_comb_func();
        even_half = (uint32_t)refill_even_line_comb_func().bits(word * 16 + 15, word * 16);
        odd_half = (uint32_t)refill_odd_line_comb_func().bits(word * 16 + 15, word * 16);
        if (req_addr_reg & 0x2) {
            // Half-word shifted access: low half comes from current odd half, high half from next even half.
            even_half = 0;
            if (word + 1 < LINE_WORDS) {
                even_half = (uint32_t)refill_even_line_comb_func().bits((word + 1) * 16 + 15, (word + 1) * 16);
            }
            refill_data_comb = odd_half | (even_half << 16);
        }
        else {
            refill_data_comb = even_half | (odd_half << 16);
        }
        return refill_data_comb;
    }

    // Direct memory bypass data, including unaligned words inside or across a memory beat.
    _LAZY_COMB(direct_data_comb, uint32_t)
        uint32_t byte;
        uint32_t word;
        byte = 0;
        word = 0;
        if (req_cache_disable_reg && ((uint32_t)req_addr_reg & 3u) != 0 &&
            (((uint32_t)req_addr_reg >> 2) & (LINE_WORDS - 1)) == LINE_WORDS - 1) {
            // L2 returns an assembled cross-line direct read in the low 32 bits of the beat.
            direct_data_comb = (uint32_t)mem_read_data_in().bits(31, 0);
        }
        else {
            word = (((uint32_t)req_addr_reg % PORT_BYTES) / 4u);
            byte = (uint32_t)req_addr_reg & 3u;
            // Start with the addressed word shifted down by the byte offset.
            direct_data_comb = (uint32_t)mem_read_data_in().bits(word * 32 + 31, word * 32) >> (byte * 8u);
            if (byte != 0 && word + 1 < PORT_WORDS) {
                // Unaligned access within the same beat: take the remaining high bytes from the next word.
                direct_data_comb |= (uint32_t)mem_read_data_in().bits((word + 1) * 32 + 31, (word + 1) * 32) << (32u - byte * 8u);
            }
            else if (byte != 0) {
                // Unaligned access at the end of the beat: the high bytes wrap to word zero of the next beat.
                direct_data_comb |= (uint32_t)mem_read_data_in().bits(31, 0) << (32u - byte * 8u);
            }
        }
        return direct_data_comb;
    }

    // Associative tag compare for the registered request.
    _LAZY_COMB(hit_comb, bool)
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

    // Select and assemble the 32-bit read word from even/odd 16-bit line RAMs.
    _LAZY_COMB(cache_data_comb, uint32_t)
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
                    // Half-word shifted cached read mirrors refill assembly: current odd half plus next even half.
                    even_half = 0;
                    if (word + 1 < LINE_WORDS) {
                        even_half = (uint32_t)even_ram[i].q_out().bits((word + 1) * 16 + 15, (word + 1) * 16);
                    }
                    cache_data_comb = odd_half | (even_half << 16);
                }
                else {
                    cache_data_comb = even_half | (odd_half << 16);
                }
            }
        }
        return cache_data_comb;
    }

    // CPU read data mux: cached hit, held refill result, or direct memory bypass.
    _LAZY_COMB(read_data_comb, uint32_t)
        if (state_reg == ST_LOOKUP && req_read_reg && hit_comb_func()) {
            read_data_comb = cache_data_comb_func();
        }
        else if (last_valid_reg) {
            read_data_comb = last_data_reg;
        }
        else {
            read_data_comb = direct_data_comb_func();
        }
        return read_data_comb;
    }

    // Address associated with the current CPU read data.
    _LAZY_COMB(read_addr_comb, uint32_t)
        return read_addr_comb = last_valid_reg ? (uint32_t)last_addr_reg : (uint32_t)req_addr_reg;
    }

    // CPU read response valid pulse/hold.
    _LAZY_COMB(read_valid_comb, bool)
        return read_valid_comb = last_valid_reg || (state_reg == ST_LOOKUP && req_read_reg && hit_comb_func());
    }

    // CPU stall request while the cache cannot return/accept data.
    _LAZY_COMB(busy_comb, bool)
        if (state_reg == ST_INIT || state_reg == ST_REFILL) {
            busy_comb = true;
        }
        else if (state_reg == ST_LOOKUP && req_read_reg && !hit_comb_func()) {
            busy_comb = true;
        }
        else {
            busy_comb = false;
        }
        return busy_comb;
    }

    // Performance/debug snapshot for the current L1 state.
    _LAZY_COMB(perf_comb, L1CachePerf)
        perf_comb.state = state_reg;
        perf_comb.hit = hit_comb_func();
        perf_comb.lookup_wait = busy_comb_func() && state_reg == ST_LOOKUP;
        perf_comb.refill_wait = busy_comb_func() && state_reg == ST_REFILL;
        perf_comb.init_wait = busy_comb_func() && state_reg == ST_INIT;
        perf_comb.issue_wait = read_in() && state_reg == ST_IDLE;
        return perf_comb;
    }

    // Backing memory read request.
    _LAZY_COMB(mem_read_comb, bool)
        return mem_read_comb = state_reg == ST_REFILL && req_read_reg;
    }

    // Backing memory address: refill word address or direct request address.
    _LAZY_COMB(mem_addr_comb, uint32_t)
        if (state_reg == ST_REFILL && req_read_reg && req_cacheable_reg) {
            mem_addr_comb = ((uint32_t)req_addr_reg & ~(uint32_t)(CACHE_LINE_SIZE - 1)) + ((uint32_t)refill_beat_reg * PORT_BYTES);
        }
        else if (state_reg == ST_REFILL && req_read_reg) {
            if (ID != 0 && !req_cache_disable_reg) {
                // Odd byte/half data loads are served as a direct beat read because
                // this L1 stores cached data in 16-bit banks. Keep the backing L2
                // request inside the containing beat so dirty L2 lines are still hit.
                mem_addr_comb = (uint32_t)req_addr_reg & ~(uint32_t)(PORT_BYTES - 1);
            }
            else {
                mem_addr_comb = req_addr_reg;
            }
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

        if (invalidate_in()) {
            req_read_reg._next = false;
            last_valid_reg._next = false;
            init_set_reg._next = 0;
            state_reg._next = ST_INIT;
        }
        else if (flush_in()) {
            req_addr_reg._next = addr_in();
            req_read_reg._next = read_in();
            req_cacheable_reg._next = input_cacheable_comb_func();
            req_cache_disable_reg._next = cache_disable_in();
            last_valid_reg._next = false;
            state_reg._next = read_in() ? ST_LOOKUP : ST_IDLE;
        }
        else if (state_reg == ST_INIT) {
            req_read_reg._next = false;
            last_valid_reg._next = false;
            if (init_set_reg == SETS - 1) {
                state_reg._next = ST_IDLE;
            }
            else {
                init_set_reg._next = init_set_reg + 1;
            }
        }
        else if (state_reg == ST_IDLE) {
            last_valid_reg._next = false;
            if (read_in() && !stall_in()) {
                req_addr_reg._next = addr_in();
                req_read_reg._next = true;
                req_cacheable_reg._next = input_cacheable_comb_func();
                req_cache_disable_reg._next = cache_disable_in();
                state_reg._next = ST_LOOKUP;
            }
        }
        else if (state_reg == ST_LOOKUP && req_read_reg) {
            if (hit_comb_func()) {
                if (stall_in()) {
                    // Downstream stall: hold the assembled hit data stable in ST_DONE.
                    last_addr_reg._next = req_addr_reg;
                    last_data_reg._next = cache_data_comb_func();
                    last_valid_reg._next = true;
                    state_reg._next = ST_DONE;
                }
                else if (start_read_comb_func()) {
                    req_addr_reg._next = addr_in();
                    req_cacheable_reg._next = input_cacheable_comb_func();
                    req_cache_disable_reg._next = cache_disable_in();
                    last_valid_reg._next = false;
                    state_reg._next = ST_LOOKUP;
                }
                else {
                    req_read_reg._next = false;
                    req_cacheable_reg._next = false;
                    last_valid_reg._next = false;
                    state_reg._next = ST_IDLE;
                }
            }
            else {
                // Miss path starts a PORT_BITWIDTH beat stream and rebuilds the split even/odd line image.
                refill_beat_reg._next = 0;
                refill_even_line_reg._next = 0;
                refill_odd_line_reg._next = 0;
                state_reg._next = ST_REFILL;
            }
        }
        else if (state_reg == ST_REFILL && req_read_reg) {
            if (req_cacheable_reg) {
                if (!mem_wait_in()) {
                    // Each accepted beat updates the partial line image; the final beat commits tag/data RAMs.
                    refill_even_line_reg._next = refill_even_line_comb_func();
                    refill_odd_line_reg._next = refill_odd_line_comb_func();
                    if (refill_beat_reg == REFILL_BEATS - 1) {
                        last_addr_reg._next = req_addr_reg;
                        last_data_reg._next = refill_data_comb_func();
                        last_valid_reg._next = true;
                        victim_reg._next = victim_reg + 1;
                        state_reg._next = ST_DONE;
                    }
                    else {
                        refill_beat_reg._next = refill_beat_reg + 1;
                    }
                }
            }
            else {
                if (!mem_wait_in()) {
                    last_addr_reg._next = req_addr_reg;
                    last_data_reg._next = direct_data_comb_func();
                    last_valid_reg._next = true;
                    state_reg._next = ST_DONE;
                }
            }
        }
        else if (state_reg == ST_DONE && !stall_in()) {
            last_valid_reg._next = false;
            if (start_read_comb_func()) {
                req_addr_reg._next = addr_in();
                req_read_reg._next = true;
                req_cacheable_reg._next = input_cacheable_comb_func();
                req_cache_disable_reg._next = cache_disable_in();
                state_reg._next = ST_LOOKUP;
            }
            else {
                req_read_reg._next = false;
                req_cacheable_reg._next = false;
                state_reg._next = ST_IDLE;
            }
        }

        if (write_in()) {
            last_valid_reg._next = false;
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
            req_cache_disable_reg.clr();
            refill_beat_reg.clr();
            victim_reg.clr();
            init_set_reg.clr();
            last_addr_reg.clr();
            last_data_reg.clr();
            last_valid_reg.clr();
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
        req_cache_disable_reg.strobe();
        refill_beat_reg.strobe();
        victim_reg.strobe();
        init_set_reg.strobe();
        last_addr_reg.strobe();
        last_data_reg.strobe();
        last_valid_reg.strobe();
        refill_even_line_reg.strobe();
        refill_odd_line_reg.strobe();
        size_t i;
        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._strobe();
            odd_ram[i]._strobe();
            tag_ram[i]._strobe();
        }
    }
};
