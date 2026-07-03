#pragma once

#include "cpphdl.h"
#include "RAM.h"
#include "L1MemIf.h"

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

/*
L1Cache behavior map

Request-facing actions that can directly answer or retire a CPU request:

1. Accept a CPU read in ST_IDLE so the cache can start a lookup, achieved by
   latching addr_in() and request attributes into req_* registers and issuing
   tag/data RAM reads.
   1.1. Reject stalled input so the CPU does not lose a request; only accept
        read_in() when stall_in() is false.
   1.2. Classify the address so L1 uses the right path; write
        req_cacheable_reg from input_cacheable_comb_func() and
        req_cache_disable_reg from cache_disable_in().
   1.3. Latch req_addr_reg and req_read_reg so ST_LOOKUP works from stable
        request state instead of the live CPU inputs.
   1.4. Start tag/data RAM reads for cacheable requests so the next cycle has
        lookup data; issue_read_comb_func() drives rd_in and
        input_set_comb_func() drives tag_ram[]/even_ram[]/odd_ram[] addresses.

2. Service a cached read hit in ST_LOOKUP so the CPU gets data without an L2
   access, achieved by tag comparison and even/odd RAM word assembly.
   2.1. Find the matching way so cached data can be trusted;
        hit_comb_func() compares valid tag_ram[] entries against
        req_tag_comb_func().
   2.2. Assemble the requested 32-bit word so aligned and supported unaligned
        reads return the same format; cache_data_comb_func() combines
        even_ram[] and odd_ram[] half-line data using req_word_comb_func() and
        the low address bits.
   2.3. Present the hit immediately when downstream is ready so no extra state
        is needed; read_valid_comb_func() asserts and read_data_comb_func()
        selects cache_data_comb_func().
   2.4. Hold a hit result when downstream stalls so read data remains stable;
        write last_addr_reg, last_data_reg, and last_valid_reg, then enter
        ST_DONE.
   2.5. Chain a different incoming read after a hit so sequential fetch/load
        traffic can continue; start_read_comb_func() accepts addr_in() when it
        differs from req_addr_reg and keeps state_reg in ST_LOOKUP.
   2.6. Retire the request after an accepted hit so the cache becomes idle;
        clear req_read_reg, req_cacheable_reg, and last_valid_reg when no new
        read is started.

3. Complete a held CPU response in ST_DONE so downstream backpressure can be
   removed without recomputing the lookup, achieved by replaying last_* regs.
   3.1. Drive the held data so the CPU sees the same response until accepted;
        read_data_comb_func() returns last_data_reg and read_addr_comb_func()
        returns last_addr_reg while last_valid_reg is true.
   3.2. Release the held response when stall_in() deasserts so the cache can
        accept more work; clear last_valid_reg.
   3.3. Accept a following read immediately after the held response so no bubble
        is forced; start_read_comb_func() reloads req_addr_reg,
        req_cacheable_reg, and req_cache_disable_reg and returns to ST_LOOKUP.
   3.4. Return to ST_IDLE when no following read is available so the next CPU
        request can be accepted normally.

4. Bypass a non-cacheable or disabled-cache read so unsupported accesses still
   complete, achieved by ST_REFILL issuing one direct backing-memory read and
   ST_DONE returning direct_data_comb_func().
   4.1. Mark odd-byte data reads, final-word line-crossing reads, explicit
        cache-disable reads, and selected I-side line-crossing reads as
        non-cacheable with input_cacheable_comb_func().
   4.2. Send the backing-memory address directly so L2 can handle alignment and
        coherency; mem_addr_comb_func() uses req_addr_reg or a containing beat
        address depending on DCACHE, req_cache_disable_reg, and
        direct_cross_beat_read_comb_func().
   4.3. Wait for mem_out.wait_out() to deassert so the returned beat is valid.
   4.4. Assemble the direct 32-bit result so unaligned words inside or across a
        PORT_BITWIDTH beat are correct; direct_data_comb_func() selects and
        shifts mem_out.read_data_out().
   4.5. Store the direct result in last_addr_reg/last_data_reg/last_valid_reg
        and enter ST_DONE so the CPU response protocol matches cached misses.

5. Retire a CPU write as a write-through/no-write-allocate operation, achieved
   by forwarding the write to memory and invalidating the matching L1 set.
   5.1. Forward every CPU write so L2 receives the store; mem_out.write_in,
        mem_out.write_data_in, and mem_out.write_mask_in directly follow write_in(),
        write_data_in(), and write_mask_in().
   5.2. Invalidate possible stale L1 hits for that set so later reads refetch
        coherent data; tag_ram[].wr_in is asserted on write_in() and
        tag_ram[].data_in writes zero.
   5.3. Clear last_valid_reg on any write so an older held read cannot be
        replayed after a store changes memory ordering.

Background and delayed activities:

1. Reset or runtime invalidate removes stale tag visibility, with the goal of
   preventing stale L1 hits. Reset uses ST_INIT to clear all tag RAM entries;
   runtime invalidate_in() flips tag_epoch_reg so old entries stop matching
   without walking every set.
   1.1. Enter ST_INIT on reset so all tags are cleared before normal operation
        resumes.
   1.2. Clear request and held-response state so no old transaction survives
        invalidation; reset req_read_reg, last_valid_reg, and
        refill_req_data_valid_reg.
   1.3. Clear one set per cycle so every way becomes invalid; tag_ram[].addr_in
        uses init_set_reg, tag_ram[].wr_in is asserted, and tag_ram[].data_in
        writes zero.
   1.4. Flip tag_epoch_reg on runtime invalidate_in() and return to ST_IDLE so
        Linux fence-heavy paths do not pay one cycle per cache set.
   1.5. Return to ST_IDLE after init_set_reg reaches SETS - 1 so normal
        request acceptance can restart.

2. Flush initiates an immediate redirect lookup, with the goal of discarding
   stale in-flight read state and following the new CPU address, achieved by
   reloading req_* from addr_in()/read_in().
   2.1. Drop the held response so redirected control flow cannot consume old
        data; clear last_valid_reg and refill_req_data_valid_reg.
   2.2. Capture the redirected request so the next state reflects the flush
        target; write req_addr_reg, req_read_reg, req_cacheable_reg, and
        req_cache_disable_reg from current inputs.
   2.3. Move to ST_LOOKUP only when read_in() is active; otherwise return to
        ST_IDLE with no pending request.

3. A cache miss initiates line refill, with the goal of installing a complete
   32-byte line and eventually answering the stalled read, achieved by streaming
   PORT_BITWIDTH beats through ST_REFILL.
   3.1. Clear partial refill state so the new line starts from a known image;
        reset refill_beat_reg, refill_even_line_reg, refill_odd_line_reg, and
        refill_req_data_valid_reg.
   3.2. Drive mem_out.read_in while ST_REFILL is active so backing memory supplies
        refill beats for the registered request.
   3.3. Generate the refill beat address so each memory beat maps into the
        cache line; mem_addr_comb_func() adds refill_beat_reg * PORT_BYTES to
        the line-aligned req_addr_reg.
   3.4. Wait while mem_out.wait_out() is true so partial line registers only update
        from accepted memory data.

4. Each accepted cacheable refill beat updates the pending line image, with the
   goal of reconstructing the split even/odd RAM format, achieved by
   refill_even_line_reg and refill_odd_line_reg accumulation.
   4.1. Store low 16-bit halves into the even image so even_ram[] receives its
        final line format; refill_even_line_comb_func() copies bits [15:0] from
        each 32-bit word in mem_out.read_data_out().
   4.2. Store high 16-bit halves into the odd image so odd_ram[] receives its
        final line format; refill_odd_line_comb_func() copies bits [31:16] from
        each 32-bit word in mem_out.read_data_out().
   4.3. Preserve an early aligned requested beat so later refill beats cannot
        overwrite the response value; update refill_req_data_reg and
        refill_req_data_valid_reg when refill_beat_reg matches
        req_refill_beat_comb_func().
   4.4. Advance refill_beat_reg after non-final accepted beats so the next
        memory read fetches the next PORT_BITWIDTH part of the line.

5. The final cacheable refill beat installs the line and prepares the CPU
   response, achieved by writing the victim way and updating last_* registers.
   5.1. Write the completed split line into the selected victim way so later
        lookups can hit; even_ram[] and odd_ram[] write when refill_beat_reg is
        REFILL_BEATS - 1 and victim_reg selects the way.
   5.2. Install the valid tag so the new line is visible to hit_comb_func();
        tag_ram[victim_reg] writes refill_tag_comb_func().
   5.3. Prepare the CPU response from the requested word so the miss completes
        without another lookup; choose direct_data_comb_func(),
        refill_req_data_reg, or refill_data_comb_func() into last_data_reg.
   5.4. Mark the response valid and advance replacement state so the next miss
        uses a different way; set last_valid_reg and update victim_reg
        round-robin.
   5.5. Enter ST_DONE so downstream stall handling is identical for hits,
        cacheable misses, and direct reads.

6. RAM and performance outputs update continuously, with the goal of keeping
   memory ports and debug state consistent with the active state.
   6.1. Drive even_ram[]/odd_ram[]/tag_ram[] addresses from input_set_comb_func()
        for new lookup issue, from req_set_comb_func() for held lookup/refill,
        or from init_set_reg during reset initialization.
   6.2. Report cache availability to the CPU so upstream can stall correctly;
        busy_comb_func() asserts during ST_INIT, ST_REFILL, and unresolved
        ST_LOOKUP misses.
   6.3. Report read response identity and data so downstream can match the
        completed access; read_addr_comb_func(), read_data_comb_func(), and
        read_valid_comb_func() select between live hit and held last_* state.
   6.4. Publish perf_out so tests and profiling can identify hit, lookup wait,
        refill wait, init wait, issue wait, and state_reg.
*/

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 2, int DCACHE = 0, size_t ADDR_BITS = 32, size_t PORT_BITWIDTH = 32>
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

    L1MemIf<PORT_BITWIDTH> mem_out;
    _PORT(L1CachePerf) perf_out = _ASSIGN_COMB(perf_comb_func());

    bool debugen_in;

    void _assign()
    {
        size_t i;
        mem_out.write_in = _ASSIGN(write_in());
        mem_out.write_data_in = _ASSIGN(write_data_in());
        mem_out.write_mask_in = _ASSIGN(write_mask_in());
        mem_out.read_in = _ASSIGN_COMB(mem_read_comb_func());
        mem_out.addr_in = _ASSIGN_COMB(mem_addr_comb_func());
        for (i = 0; i < WAYS; ++i) {
            even_ram[i].addr_in = _ASSIGN((state_reg == ST_REFILL || (state_reg == ST_LOOKUP && !issue_read_comb_func())) ? req_set_comb_func() : input_set_comb_func());
            even_ram[i].data_in = _ASSIGN(refill_even_line_comb_func());
            even_ram[i].wr_in = _ASSIGN_I((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_beat_reg == REFILL_BEATS - 1 && victim_reg == i);
            even_ram[i].rd_in = _ASSIGN(issue_read_comb_func() && input_cacheable_comb_func());
            even_ram[i].id_in = DCACHE * 100 + i * 3;

            odd_ram[i].addr_in = _ASSIGN((state_reg == ST_REFILL || (state_reg == ST_LOOKUP && !issue_read_comb_func())) ? req_set_comb_func() : input_set_comb_func());
            odd_ram[i].data_in = _ASSIGN(refill_odd_line_comb_func());
            odd_ram[i].wr_in = _ASSIGN_I((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_beat_reg == REFILL_BEATS - 1 && victim_reg == i);
            odd_ram[i].rd_in = _ASSIGN(issue_read_comb_func() && input_cacheable_comb_func());
            odd_ram[i].id_in = DCACHE * 100 + i * 3 + 1;

            tag_ram[i].addr_in = _ASSIGN((state_reg == ST_INIT) ? init_set_reg :
                                        (write_in() ? input_set_comb_func() :
                                         ((state_reg == ST_REFILL || (state_reg == ST_LOOKUP && !issue_read_comb_func())) ? req_set_comb_func() : input_set_comb_func())));
            tag_ram[i].data_in = _ASSIGN((state_reg == ST_REFILL) ? refill_tag_comb_func() : logic<TAG_BITS + 2>(0));
            tag_ram[i].wr_in = _ASSIGN_I((state_reg == ST_INIT) ||
                                        ((state_reg == ST_REFILL) && req_read_reg && req_cacheable_reg && refill_beat_reg == REFILL_BEATS - 1 && victim_reg == i) ||
                                        write_in());
            tag_ram[i].rd_in = _ASSIGN(issue_read_comb_func() && input_cacheable_comb_func());
            tag_ram[i].id_in = DCACHE * 100 + i * 3 + 2;
        }
    }

private:
    RAM<HALF_LINE_BITS, SETS> even_ram[WAYS];
    RAM<HALF_LINE_BITS, SETS> odd_ram[WAYS];
    RAM<TAG_BITS + 2, SETS> tag_ram[WAYS];

    reg<u<3>> state_reg;
    reg<u32> req_addr_reg;
    reg<u1> req_read_reg;
    reg<u1> req_cacheable_reg;
    reg<u1> req_cache_disable_reg;
    reg<u1> tag_epoch_reg;
    reg<u<REFILL_BEAT_BITS>> refill_beat_reg;
    reg<u<WAY_BITS>> victim_reg;
    reg<u<SET_BITS>> init_set_reg;
    reg<u32> last_addr_reg;
    reg<u32> last_data_reg;
    reg<u1> last_valid_reg;
    reg<logic<HALF_LINE_BITS>> refill_even_line_reg;
    reg<logic<HALF_LINE_BITS>> refill_odd_line_reg;
    reg<u32> refill_req_data_reg;
    reg<u1> refill_req_data_valid_reg;

    // Set index of the registered request address.
    _LAZY_COMB(req_set_comb, uint32_t)
        return req_set_comb = ((uint32_t)req_addr_reg / CACHE_LINE_SIZE) % SETS;
    }

    // Tag bits of the registered request address.
    _LAZY_COMB(req_tag_comb, uint32_t)
        return req_tag_comb = (uint32_t)req_addr_reg / (CACHE_LINE_SIZE * SETS);
    }

    // PORT_BITWIDTH beat number that contains the requested word in a line refill.
    _LAZY_COMB(req_refill_beat_comb, u<REFILL_BEAT_BITS>)
        return req_refill_beat_comb = (u<REFILL_BEAT_BITS>)(((uint32_t)req_addr_reg & (CACHE_LINE_SIZE - 1)) / PORT_BYTES);
    }

    // 32-bit word index inside the registered cache line.
    _LAZY_COMB(req_word_comb, u<WORD_BITS>)
        return req_word_comb = (u<WORD_BITS>)(((uint32_t)req_addr_reg >> 2) & (LINE_WORDS - 1));
    }

    // Set index of the incoming CPU address.
    _LAZY_COMB(input_set_comb, uint32_t)
        return input_set_comb = (addr_in() / CACHE_LINE_SIZE) % SETS;
    }

    // Whether the incoming address can be served from this cache line format.
    // Odd-byte reads use a direct L2 beat because cached data is stored in
    // 16-bit banks. Even unaligned reads can still be assembled from halves.
    _LAZY_COMB(input_cacheable_comb, bool)
        input_cacheable_comb = !cache_disable_in() && !(addr_in() & 0x1);
        if (DCACHE != 0 && (addr_in() & 0x3u) != 0 &&
            (((addr_in() >> 2) & (LINE_WORDS - 1)) == LINE_WORDS - 1)) {
            // A 32-bit read starting at the final line word needs bytes from the next line.
            // CPU split logic handles real line-crossing accesses before L2 sees a cacheable line fill.
            input_cacheable_comb = false;
        }
        if (DCACHE == 0 && (addr_in() & 0x2u) != 0 &&
            (((addr_in() >> 2) & (LINE_WORDS - 1)) == LINE_WORDS - 1)) {
            // A 32-bit instruction at the final halfword spans two cache lines.
            // Bypass the line RAM so L2 can return the assembled instruction word.
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
            if (state_reg == ST_DONE && req_cacheable_reg && addr_in() != (uint32_t)last_addr_reg) {
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

    // Tag RAM payload written at the end of a refill: valid bit, epoch bit, and tag.
    _LAZY_COMB(refill_tag_comb, logic<TAG_BITS + 2>)
        // Write the epoch bit explicitly instead of packing it through an
        // integer cast from reg<u1>; after an invalidate the cast can describe
        // the old epoch and make every new line miss.
        refill_tag_comb = req_tag_comb_func();
        refill_tag_comb[TAG_BITS] = (bool)tag_epoch_reg;
        refill_tag_comb[TAG_BITS + 1] = true;
        return refill_tag_comb;
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
                (uint32_t)mem_out.read_data_out().bits(i * 32 + 15, i * 32);
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
                (uint32_t)mem_out.read_data_out().bits(i * 32 + 31, i * 32 + 16);
        }
        return refill_odd_line_comb;
    }

    // Requested word assembled from the completed refill line image.
    _LAZY_COMB(refill_data_comb, uint32_t)
        uint32_t word;
        uint32_t byte;
        uint32_t word_data;
        uint32_t next_word_data;
        uint32_t even_half;
        uint32_t odd_half;
        word = (uint32_t)req_word_comb_func();
        byte = (uint32_t)req_addr_reg & 3u;
        even_half = (uint32_t)refill_even_line_comb_func().bits(word * 16 + 15, word * 16);
        odd_half = (uint32_t)refill_odd_line_comb_func().bits(word * 16 + 15, word * 16);
        word_data = even_half | (odd_half << 16);
        next_word_data = 0;
        if (byte != 0) {
            if (word + 1 < LINE_WORDS) {
                even_half = (uint32_t)refill_even_line_comb_func().bits((word + 1) * 16 + 15, (word + 1) * 16);
                odd_half = (uint32_t)refill_odd_line_comb_func().bits((word + 1) * 16 + 15, (word + 1) * 16);
                next_word_data = even_half | (odd_half << 16);
            }
            refill_data_comb = (word_data >> (byte * 8u)) | (next_word_data << (32u - byte * 8u));
        }
        else {
            refill_data_comb = word_data;
        }
        return refill_data_comb;
    }

    // True when a direct 32-bit read starts in the final word of a memory beat
    // and needs high bytes from the following beat.
    _LAZY_COMB(direct_cross_beat_read_comb, bool)
        uint32_t byte;
        uint32_t word;
        byte = (uint32_t)req_addr_reg & 3u;
        word = ((uint32_t)req_addr_reg % PORT_BYTES) / 4u;
        return direct_cross_beat_read_comb = !req_cache_disable_reg && byte != 0 && word + 1 >= PORT_WORDS;
    }

    // Direct memory bypass data, including unaligned words inside or across a memory beat.
    // Cached RAM direct reads stay beat-aligned so dirty L2 data is used instead of stale backing RAM.
    _LAZY_COMB(direct_data_comb, uint32_t)
        uint32_t byte;
        uint32_t word;
        byte = 0;
        word = 0;
        if (!req_cacheable_reg && direct_cross_beat_read_comb_func()) {
            // Cross-beat direct reads return the assembled 32-bit word in the low bits.
            direct_data_comb = (uint32_t)mem_out.read_data_out().bits(31, 0);
        }
        else {
            word = (((uint32_t)req_addr_reg % PORT_BYTES) / 4u);
            byte = (uint32_t)req_addr_reg & 3u;
            // Start with the addressed word shifted down by the byte offset.
            direct_data_comb = (uint32_t)mem_out.read_data_out().bits(word * 32 + 31, word * 32) >> (byte * 8u);
            if (byte != 0 && word + 1 < PORT_WORDS) {
                // Unaligned access within the same beat: take the remaining high bytes from the next word.
                direct_data_comb |= (uint32_t)mem_out.read_data_out().bits((word + 1) * 32 + 31, (word + 1) * 32) << (32u - byte * 8u);
            }
            else if (byte != 0) {
                // Unaligned access at the end of the beat: the high bytes wrap to word zero of the next beat.
                direct_data_comb |= (uint32_t)mem_out.read_data_out().bits(31, 0) << (32u - byte * 8u);
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
                if ((bool)tag_ram[i].q_out()[TAG_BITS + 1] &&
                    (bool)tag_ram[i].q_out()[TAG_BITS] == (bool)tag_epoch_reg &&
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
        uint32_t byte;
        uint32_t word_data;
        uint32_t next_word_data;
        uint32_t even_half;
        uint32_t odd_half;
        cache_data_comb = 0;
        word = (uint32_t)req_word_comb_func();
        byte = (uint32_t)req_addr_reg & 3u;
        word_data = 0;
        next_word_data = 0;
        even_half = 0;
        odd_half = 0;
        for (i = 0; i < WAYS; ++i) {
            if ((bool)tag_ram[i].q_out()[TAG_BITS + 1] &&
                (bool)tag_ram[i].q_out()[TAG_BITS] == (bool)tag_epoch_reg &&
                tag_ram[i].q_out().bits(TAG_BITS - 1, 0) == req_tag_comb_func()) {
                even_half = (uint32_t)even_ram[i].q_out().bits(word * 16 + 15, word * 16);
                odd_half = (uint32_t)odd_ram[i].q_out().bits(word * 16 + 15, word * 16);
                word_data = even_half | (odd_half << 16);
                if (byte != 0) {
                    if (word + 1 < LINE_WORDS) {
                        even_half = (uint32_t)even_ram[i].q_out().bits((word + 1) * 16 + 15, (word + 1) * 16);
                        odd_half = (uint32_t)odd_ram[i].q_out().bits((word + 1) * 16 + 15, (word + 1) * 16);
                        next_word_data = even_half | (odd_half << 16);
                    }
                    cache_data_comb = (word_data >> (byte * 8u)) | (next_word_data << (32u - byte * 8u));
                }
                else {
                    cache_data_comb = word_data;
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
            if (DCACHE != 0 && !req_cache_disable_reg) {
                // Odd byte/half data loads are served as a direct beat read because
                // this L1 stores cached data in 16-bit banks. Keep them inside the
                // containing beat unless the requested word crosses into the next beat.
                // Cross-beat reads use the raw address so L2 can assemble both beats.
                mem_addr_comb = direct_cross_beat_read_comb_func() ?
                    (uint32_t)req_addr_reg :
                    ((uint32_t)req_addr_reg & ~(uint32_t)(PORT_BYTES - 1));
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
            refill_req_data_valid_reg._next = false;
            tag_epoch_reg._next = !tag_epoch_reg;
            state_reg._next = ST_IDLE;
        }
        else if (flush_in()) {
            req_addr_reg._next = addr_in();
            req_read_reg._next = read_in();
            req_cacheable_reg._next = input_cacheable_comb_func();
            req_cache_disable_reg._next = cache_disable_in();
            last_valid_reg._next = false;
            refill_req_data_valid_reg._next = false;
            state_reg._next = read_in() ? ST_LOOKUP : ST_IDLE;
        }
        else if (state_reg == ST_INIT) {
            req_read_reg._next = false;
            last_valid_reg._next = false;
            refill_req_data_valid_reg._next = false;
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
                refill_req_data_valid_reg._next = false;
                state_reg._next = ST_REFILL;
            }
        }
        else if (state_reg == ST_REFILL && req_read_reg) {
            if (req_cacheable_reg) {
                if (!mem_out.wait_out()) {
                    // Each accepted beat updates the partial line image. The CPU-requested
                    // beat is latched separately because later refill beats carry different words.
                    refill_even_line_reg._next = refill_even_line_comb_func();
                    refill_odd_line_reg._next = refill_odd_line_comb_func();
                    if (refill_beat_reg == req_refill_beat_comb_func() && (((uint32_t)req_addr_reg & 0x3u) == 0)) {
                        refill_req_data_reg._next = direct_data_comb_func();
                        refill_req_data_valid_reg._next = true;
                    }
                    if (refill_beat_reg == REFILL_BEATS - 1) {
                        last_addr_reg._next = req_addr_reg;
                        last_data_reg._next = (refill_beat_reg == req_refill_beat_comb_func()) ?
                            direct_data_comb_func() :
                            (refill_req_data_valid_reg ? (uint32_t)refill_req_data_reg : refill_data_comb_func());
                        last_valid_reg._next = true;
                        refill_req_data_valid_reg._next = false;
                        victim_reg._next = (victim_reg == WAYS - 1) ? u<WAY_BITS>(0) : u<WAY_BITS>(victim_reg + 1);
                        state_reg._next = ST_DONE;
                    }
                    else {
                        refill_beat_reg._next = refill_beat_reg + 1;
                    }
                }
            }
            else {
                if (!mem_out.wait_out()) {
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
            tag_epoch_reg.clr();
            refill_beat_reg.clr();
            victim_reg.clr();
            init_set_reg.clr();
            last_addr_reg.clr();
            last_data_reg.clr();
            last_valid_reg.clr();
            refill_even_line_reg.clr();
            refill_odd_line_reg.clr();
            refill_req_data_reg.clr();
            refill_req_data_valid_reg.clr();
            state_reg._next = ST_INIT;
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        state_reg.strobe(checkpoint_fd);
        req_addr_reg.strobe(checkpoint_fd);
        req_read_reg.strobe(checkpoint_fd);
        req_cacheable_reg.strobe(checkpoint_fd);
        req_cache_disable_reg.strobe(checkpoint_fd);
        tag_epoch_reg.strobe(checkpoint_fd);
        refill_beat_reg.strobe(checkpoint_fd);
        victim_reg.strobe(checkpoint_fd);
        init_set_reg.strobe(checkpoint_fd);
        last_addr_reg.strobe(checkpoint_fd);
        last_data_reg.strobe(checkpoint_fd);
        last_valid_reg.strobe(checkpoint_fd);
        refill_even_line_reg.strobe(checkpoint_fd);
        refill_odd_line_reg.strobe(checkpoint_fd);
        // Transient refill response state is intentionally not checkpointed,
        // but it still must commit every cycle while a multi-beat line arrives.
        refill_req_data_reg.strobe();
        refill_req_data_valid_reg.strobe();
        size_t i;
        for (i = 0; i < WAYS; ++i) {
            even_ram[i]._strobe(checkpoint_fd);
            odd_ram[i]._strobe(checkpoint_fd);
            tag_ram[i]._strobe(checkpoint_fd);
        }
    }
};
