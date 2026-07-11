#pragma once

#include "cpphdl.h"
#include "RAM.h"
#include "../L1MemIf.h"

using namespace cpphdl;

enum L1CacheFsmState : uint64_t
{
    L1_ST_IDLE = 0,
    L1_ST_LOOKUP = 1,
    L1_ST_DONE = 2,
    L1_ST_REFILL = 3,
    L1_ST_INIT = 4
};

struct L1CachePerf
{
    unsigned hit:1;              // Current lookup found a valid matching way.
    unsigned lookup_wait:1;      // CPU waits while a lookup resolves as a miss.
    unsigned refill_wait:1;      // CPU waits for backing-memory refill beats.
    unsigned init_wait:1;        // CPU waits while tag RAM initialization runs.
    unsigned issue_wait:1;       // CPU presents a read while the cache is idle.
    u<3> state;                  // Current L1CacheFsmState for performance diagnosis.
} __PACKED;

// Keeps all address properties derived from the registered request coherent across layers.
struct L1RequestGeometryComb
{
    u32 set;                     // RAM set selected by req_reg.addr.
    u32 tag;                     // Address tag compared by L1CacheLookup.
    u32 word;                    // Word index consumed by response assembly.
    u32 refill_beat;             // Beat containing the requested word.
    u1 direct_cross_beat;        // Direct unaligned read requires a following beat.
};

// Keeps all live-input acceptance decisions together so RAM issue and request capture cannot diverge.
struct L1InputRequestComb
{
    u32 set;                     // RAM set selected by the live CPU address.
    u1 cacheable;                // Request can use split-bank cache storage.
    u1 start;                    // Controller accepts the live request this cycle.
    u1 issue;                    // RAM read must be issued for this accepted request.
};

// Drives the complete L1-to-L2 request as one object; L1MemIf consumes these fields in _assign().
struct L1MemDriver
{
    u1 read;                     // Request backing memory to return one beat.
    u1 write;                    // Forward a CPU store to backing memory.
    u32 addr;                    // Refill, direct-read, or live CPU byte address.
    u32 write_data;              // CPU store data forwarded without reinterpretation.
    u8 write_mask;               // Byte enables associated with write_data.
};

// Carries both split-bank images produced from the same accepted refill beat.
struct L1RefillLinesComb
{
    logic<128> even;             // Low 16 bits of every line word after this beat.
    logic<128> odd;              // High 16 bits of every line word after this beat.
};

// Carries one associative lookup result so hit, selected way, and data always agree.
struct L1LookupComb
{
    u1 hit;                      // At least one current-epoch tag matches.
    u8 way;                      // Matching way used to select both data banks.
    u32 data;                    // Aligned or unaligned word assembled from that way.
};

// Carries the complete CPU-facing response and busy decision from one state snapshot.
struct L1CpuResponseComb
{
    u32 data;                    // Live hit, refill result, or held response data.
    u32 addr;                    // Address corresponding to data and valid.
    u1 valid;                    // A complete CPU response is available.
    u1 busy;                     // CPU request must remain pending.
};

// Holds every attribute captured when one CPU read becomes the active L1 request.
struct L1RequestState
{
    u32 addr;                    // Stable byte address used by lookup and refill layers.
    u1 read;                     // A read remains active until response retirement.
    u1 cacheable;                // Active request is represented by tag/data RAMs.
    u1 cache_disable;            // Explicit bypass mode used for direct address routing.
};

// Holds all partial data and progress belonging to one in-flight line refill.
struct L1RefillState
{
    u8 beat;                     // Current memory beat; lower REFILL_BEAT_BITS are used.
    logic<128> even_line;        // Accumulated low 16-bit halves of all line words.
    logic<128> odd_line;         // Accumulated high 16-bit halves of all line words.
    u32 req_data;                // Requested aligned word saved from an earlier beat.
    u1 req_data_valid;           // req_data contains the requested word.
};

// Holds one completed response while downstream applies backpressure.
struct L1HeldResponse
{
    u32 addr;                    // Address associated with the held data.
    u32 data;                    // Completed cached or direct read result.
    u1 valid;                    // Held response must remain visible to the CPU.
};

template<size_t TOTAL_CACHE_SIZE = 1024, size_t CACHE_LINE_SIZE = 32,
    size_t WAYS = 2, int DCACHE = 0, size_t ADDR_BITS = 32,
    size_t PORT_BITWIDTH = 32>
// Owns L1 ports, RAMs, and registers; behavior is supplied only by derived operation layers.
class L1CacheState : public Module
{
protected:
    // Number of words in a line; used by request decode and refill assembly.
    static constexpr size_t LINE_WORDS = CACHE_LINE_SIZE / 4;
    // Number of tag/data RAM rows; used by RAM instances and initialization.
    static constexpr size_t SETS = TOTAL_CACHE_SIZE / CACHE_LINE_SIZE / WAYS;
    // Width of RAM row selectors; used by init_set_reg and RAM ports.
    static constexpr size_t SET_BITS = clog2(SETS);
    // Width of a line-local word selector; retained for datapath sizing.
    static constexpr size_t WORD_BITS = clog2(LINE_WORDS);
    // Width of the line byte offset; used to derive TAG_BITS.
    static constexpr size_t LINE_BITS = clog2(CACHE_LINE_SIZE);
    // Width of each split data RAM; used by line and refill registers.
    static constexpr size_t HALF_LINE_BITS = CACHE_LINE_SIZE * 4;
    // Bytes per backing-memory beat; used by request and refill layers.
    static constexpr size_t PORT_BYTES = PORT_BITWIDTH / 8;
    // Words per backing-memory beat; used by refill split and response glue.
    static constexpr size_t PORT_WORDS = PORT_BITWIDTH / 32;
    // Number of accepted beats per line refill; used by the controller.
    static constexpr size_t REFILL_BEATS = CACHE_LINE_SIZE / PORT_BYTES;
    // Active refill beat width; retained for calculations while refill_reg uses a concrete u8.
    static constexpr size_t REFILL_BEAT_BITS = REFILL_BEATS <= 1 ? 1 : clog2(REFILL_BEATS);
    // Address tag width; used by tag RAM and lookup/refill-tag layers.
    static constexpr size_t TAG_BITS = ADDR_BITS - SET_BITS - LINE_BITS;
    // Width of victim_reg; one bit is retained for direct-mapped caches.
    static constexpr size_t WAY_BITS = WAYS <= 1 ? 1 : clog2(WAYS);

    static_assert(CACHE_LINE_SIZE == 32, "L1Cache uses 32-byte cache lines");
    static_assert(PORT_BITWIDTH >= 32 && PORT_BITWIDTH % 32 == 0,
        "L1Cache refill port must be a whole number of 32-bit words");
    static_assert((CACHE_LINE_SIZE * 8) % PORT_BITWIDTH == 0,
        "L1Cache refill port must divide a cache line");
    static_assert(WAYS > 0, "L1Cache needs at least one way");
    static_assert(TOTAL_CACHE_SIZE % (CACHE_LINE_SIZE * WAYS) == 0,
        "L1Cache geometry must divide evenly");
    static_assert(ADDR_BITS > SET_BITS + LINE_BITS,
        "L1Cache address width must include tag bits");

public:
    _PORT(bool) write_in;
    _PORT(uint32_t) write_data_in;
    _PORT(uint8_t) write_mask_in;
    _PORT(bool) read_in;
    _PORT(uint32_t) addr_in;
    _PORT(uint32_t) read_data_out;
    _PORT(uint32_t) read_addr_out;
    _PORT(bool) read_valid_out;
    _PORT(bool) busy_out;
    _PORT(bool) stall_in;
    _PORT(bool) flush_in;
    _PORT(bool) invalidate_in;
    _PORT(bool) cache_disable_in;
    L1MemIf<PORT_BITWIDTH> mem_out;
    _PORT(L1CachePerf) perf_out;
    bool debugen_in;

protected:
    RAM<HALF_LINE_BITS, SETS> even_ram[WAYS];
    RAM<HALF_LINE_BITS, SETS> odd_ram[WAYS];
    RAM<TAG_BITS + 2, SETS> tag_ram[WAYS];

    reg<u<3>> state_reg;
    reg<L1RequestState> req_reg;
    reg<u1> tag_epoch_reg;
    reg<L1RefillState> refill_reg;
    reg<u<WAY_BITS>> victim_reg;
    reg<u<SET_BITS>> init_set_reg;
    reg<L1HeldResponse> response_reg;
};
