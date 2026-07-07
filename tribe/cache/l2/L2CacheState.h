#pragma once

#include "cpphdl.h"
#include "Axi4.h"
#include "../L1MemIf.h"

using namespace cpphdl;

// L2 FSM states live at header scope so dependent derived template layers can
// use ST_* names directly without importing every enumerator from a base class.
enum L2CacheFsmState : uint64_t
{
    ST_IDLE = 0,
    ST_INIT = 1,
    ST_LOOKUP = 2,
    ST_AXI_AR = 3,
    ST_AXI_R = 4,
    ST_DONE = 5,
    ST_CROSS_AR0 = 6,
    ST_CROSS_R0 = 7,
    ST_CROSS_AR1 = 8,
    ST_CROSS_R1 = 9,
    ST_EVICT_AW = 10,
    ST_EVICT_W = 11,
    ST_EVICT_B = 12,
    ST_CROSS_WRITE_LOOKUP = 13,
    ST_CROSS_DONE = 14,
    ST_IO_AW = 15,
    ST_IO_W = 16,
    ST_IO_B = 17,
    ST_IO_AR = 18,
    ST_IO_R = 19,
    ST_READ = 20
};

// Captured L2 request payload after arbitration. Widths use the cache-supported
// maxima so the struct package is concrete even when the Module keeps
// PORT_BITWIDTH and MEM_PORTS as SystemVerilog parameters.
struct L2MemDriver
{
    u32 addr;                    // Full physical address selected from I, D, or AXI slave input.
    u32 write_data;              // Low 32-bit CPU/L1 store data before byte-lane alignment.
    logic<256> write_beat;       // Full AXI slave write beat; lower PORT_BITWIDTH bits are used.
    u8 write_mask;               // CPU/L1 byte mask for the 32-bit store data.
    logic<32> write_strobe;      // AXI byte strobe; lower PORT_BYTES bits are used.
    logic<8> write_word_mask;    // AXI word-enable mask; lower PORT_WORDS bits are used.
    u1 read;                     // Request is a read transaction.
    u1 write;                    // Request is a write transaction.
    u1 port;                     // Request came from the CPU/L1 data port rather than instruction port.
    u1 from_slave;               // Request came from an external coherent AXI slave port.
    u8 slave_index;              // External AXI slave port index; lower MEM_PORT_BITS bits are used.
    u<4> slave_id;               // External AXI transaction ID to echo in the response.
};

// Combinational AXI responder fields driven toward external coherent masters.
// Concrete maximum widths avoid unresolved parameterized packages in generated SV.
struct L2AxiResponderComb
{
    u1 aw_ready;                 // Accept an external AXI write address when this slave slot can track it.
    u1 w_ready;                  // Accept external AXI write data when the selected write request is active.
    u1 b_valid;                  // Present a completed external AXI write response.
    u<4> b_id;                   // Echo the completed external AXI write ID.
    u1 ar_ready;                 // Accept an external AXI read address when this slave slot wins arbitration.
    u1 r_valid;                  // Present a completed external AXI read response.
    logic<256> r_data;           // Read response data; lower PORT_BITWIDTH bits are used.
    u1 r_last;                   // L2 responds as one-beat AXI transactions.
    u<4> r_id;                   // Echo the completed external AXI read ID.
};

// Combinational AXI driver fields driven toward RAM/device regions.
// Concrete maximum widths avoid unresolved parameterized packages in generated SV.
struct L2AxiDriverComb
{
    u1 aw_valid;                 // Start a write address transaction on the selected memory/device port.
    u32 aw_addr;                 // Local address inside the selected memory/device region.
    u<4> aw_id;                  // L2 master writes use ID zero.
    u1 w_valid;                  // Present write data for the selected memory/device port.
    logic<256> w_data;           // Write data beat; lower PORT_BITWIDTH bits are used.
    logic<32> w_strb;            // Write byte enables; lower PORT_BYTES bits are used.
    u1 w_last;                   // L2 emits one-beat AXI writes.
    u1 b_ready;                  // Accept write response from the selected memory/device port.
    u1 ar_valid;                 // Start a read address transaction on the selected memory/device port.
    u32 ar_addr;                 // Local address inside the selected memory/device region.
    u<4> ar_id;                  // L2 master reads use ID zero.
    u1 r_ready;                  // Accept read data from the selected memory/device port.
};

template<size_t CACHE_SIZE = 16384, size_t PORT_BITWIDTH = 256, size_t CACHE_LINE_SIZE = 32, size_t WAYS = 4, size_t ADDR_BITS = 32, size_t MEM_ADDR_BITS = ADDR_BITS, size_t MEM_PORTS = 1>
// Owns the external L2 protocol ports, RAM arrays, state registers, and geometry constants.
class L2CacheState : public Module
{
protected:
    static_assert(CACHE_LINE_SIZE == 32, "L2Cache uses 32-byte cache lines");
    static_assert(PORT_BITWIDTH >= 32 && PORT_BITWIDTH % 32 == 0, "L2Cache port must be a whole number of 32-bit words");
    static_assert((CACHE_LINE_SIZE * 8) % PORT_BITWIDTH == 0, "L2Cache port must divide a cache line");
    static_assert(WAYS > 0, "L2Cache needs at least one way");
    static_assert(CACHE_SIZE % (CACHE_LINE_SIZE * WAYS) == 0, "L2Cache geometry must divide evenly");
    static_assert(MEM_PORTS >= 1, "L2Cache must have at least one memory port");
    static_assert((MEM_PORTS & (MEM_PORTS - 1)) == 0, "L2Cache memory port count must be a power of two");
    static_assert(PORT_BITWIDTH <= 256, "L2Cache AXI read response struct storage is sized for up to 256-bit ports");

    static constexpr size_t LINE_WORDS = CACHE_LINE_SIZE / 4;
    static constexpr size_t PORT_BYTES = PORT_BITWIDTH / 8;
    static constexpr size_t PORT_WORDS = PORT_BITWIDTH / 32;
    static constexpr size_t LINE_BEATS = CACHE_LINE_SIZE / PORT_BYTES;
    static constexpr size_t LINE_BEAT_BITS = LINE_BEATS <= 1 ? 1 : clog2(LINE_BEATS);
    static constexpr size_t SETS = CACHE_SIZE / CACHE_LINE_SIZE / WAYS;
    static constexpr size_t SET_BITS = clog2(SETS);
    static constexpr size_t LINE_BITS = clog2(CACHE_LINE_SIZE);
    static constexpr size_t WORD_BITS = clog2(LINE_WORDS);
    static constexpr size_t WAY_BITS = WAYS <= 1 ? 1 : clog2(WAYS);
    static constexpr size_t TAG_BITS = ADDR_BITS - SET_BITS - LINE_BITS;
    static constexpr size_t TAG_RAM_BITS = ((TAG_BITS + 2 + 7) / 8) * 8;
    static constexpr size_t DATA_BANKS = WAYS * LINE_WORDS;
    static constexpr size_t MEM_PORT_BITS = clog2(MEM_PORTS);
    static constexpr uint64_t MEM_ADDR_MASK64 = (MEM_ADDR_BITS >= 64) ? ~0ull : ((1ull << MEM_ADDR_BITS) - 1ull);

public:
    L1MemIf<PORT_BITWIDTH> i_mem_in;
    L1MemIf<PORT_BITWIDTH> d_mem_in;

    _PORT(uint32_t) memory_base_in;
    _PORT(uint32_t) memory_size_in;
    // Cumulative memory/device regions; each L2 master port owns one contiguous slice.
    _PORT(uint32_t) mem_region_size_in[MEM_PORTS];
    // Uncached regions bypass tag/data RAM and are used for MMIO/device ports.
    _PORT(bool) mem_region_uncached_in[MEM_PORTS];

    // External AXI masters enter with full physical addresses; only outgoing
    // memory/device master ports are narrowed to MEM_ADDR_BITS after routing.
    Axi4If<ADDR_BITS, 4, PORT_BITWIDTH> axi_in[MEM_PORTS];
    // L2 master ports leave here toward RAM and device regions.
    Axi4If<MEM_ADDR_BITS, 4, PORT_BITWIDTH> axi_out[MEM_PORTS];

    bool debugen_in;

protected:
    // Flattened memories avoid array-of-memory dimension reversal in generated SV.
    // Index helpers in the controller map (set, way/word-bank) into one address.
    memory<u8, 4, (CACHE_SIZE / CACHE_LINE_SIZE / WAYS) * DATA_BANKS> data_ram;
    memory<u8, (((ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 2 + 7) / 8)),
        (CACHE_SIZE / CACHE_LINE_SIZE / WAYS) * WAYS> tag_ram; // {valid, dirty, tag}
    reg<array<logic<32>, DATA_BANKS, true>> data_q_reg;
    reg<array<logic<((ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE) + 2 + 7) / 8) * 8>,
        DATA_BANKS, true>> tag_q_reg;

    reg<u<5>> state_reg;
    reg<L2MemDriver> req_reg;
    reg<u<(WAYS <= 1 ? 1 : clog2(WAYS))>> victim_reg;
    reg<u<(WAYS <= 1 ? 1 : clog2(WAYS))>> fill_way_reg;
    reg<u<clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS)>> init_set_reg;
    reg<logic<PORT_BITWIDTH>> last_data_reg;
    // External AXI reads can request an early fill beat; latch it until the
    // complete line is installed and the slave response is emitted.
    reg<logic<PORT_BITWIDTH>> slave_fill_data_reg;
    reg<logic<PORT_BITWIDTH>> cross_low_reg;
    reg<logic<PORT_BITWIDTH>> cross_high_reg;
    reg<u<((CACHE_LINE_SIZE / (PORT_BITWIDTH / 8)) <= 1 ? 1 : clog2(CACHE_LINE_SIZE / (PORT_BITWIDTH / 8)))>> fill_beat_reg;
    reg<u<((CACHE_LINE_SIZE / (PORT_BITWIDTH / 8)) <= 1 ? 1 : clog2(CACHE_LINE_SIZE / (PORT_BITWIDTH / 8)))>> evict_beat_reg;
    reg<u<ADDR_BITS - clog2(CACHE_SIZE / CACHE_LINE_SIZE / WAYS) - clog2(CACHE_LINE_SIZE)>> evict_tag_reg;
    reg<logic<CACHE_LINE_SIZE * 8>> evict_line_reg;
    static_assert(MEM_PORTS <= 8, "L2Cache AXI slave bookkeeping storage supports up to 8 ports");
    // Keep slave write responses as AXI channel structs so valid and ID are updated together.
    // The storage is fixed at 8 entries because the SV generator currently specializes struct-array
    // lengths from the first module instance; unused entries stay idle when MEM_PORTS is smaller.
    reg<array<Axi4WriteResponse<4>, 8>> slave_b_reg;
    // Keep slave read responses as AXI channel structs. The data field is fixed at 256 bits because
    // generated SV keeps PORT_BITWIDTH as a module parameter, while package structs need a concrete width.
    reg<array<Axi4ReadData<4, 256>, 8>> slave_r_reg;
    // Keep split AW state as one AXI address payload so delayed W handshakes retain address and ID together.
    reg<array<Axi4WriteAddress<ADDR_BITS, 4>, 8>> slave_aw_reg;

    // True when any external AXI master is offering a one-beat write.
};
