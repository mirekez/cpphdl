// L1CacheRefill requirements tested here:
// 1. Split every accepted refill word into matching even/odd 16-bit bank images.
// 2. Assemble aligned and unaligned words from split line images.
// 3. Select the addressed word from a direct memory beat.
#include "L1CacheRefill_test.h"
#include "L1CacheRefill.h"
#include "L1CacheTestCommon.h"

long _system_clock = 0;

class RefillProbe : public L1CacheRefill<1024, 32, 2, 1, 17, 64>
{
    logic<64> memory_data = 0;
public:
    void _assign() { mem_out.read_data_out = _ASSIGN_REG(memory_data); }
    void set(uint32_t addr, uint64_t data)
    {
        memory_data = data; req_reg._next.addr = addr; req_reg._next.cacheable = true;
        req_reg._next.cache_disable = false; refill_reg._next.beat = 0;
        refill_reg._next.even_line = 0; refill_reg._next.odd_line = 0;
        req_reg.strobe(); refill_reg.strobe();
        ++_system_clock;
    }
    L1RefillLinesComb lines() { return refill_lines_comb_func(); }
    uint32_t direct() { return direct_data_comb_func(); }
    uint32_t assemble(const logic<128>& even, const logic<128>& odd, uint32_t word, uint32_t byte)
    { return assemble_line_word(even, odd, word, byte); }
};

int L1CacheRefill_test_main()
{
    l1test::Context ctx;
    RefillProbe probe;
    probe._assign();
    probe.set(4, 0x8877665544332211ull);
    auto lines = probe.lines();
    l1test::section("1. split refill beat");
    l1test::expect_eq(ctx, "even word 0", lines.even.bits(15, 0), 0x2211);
    l1test::expect_eq(ctx, "odd word 0", lines.odd.bits(15, 0), 0x4433);
    l1test::expect_eq(ctx, "even word 1", lines.even.bits(31, 16), 0x6655);
    l1test::section("2. split-line assembly");
    l1test::expect_eq(ctx, "aligned word", probe.assemble(lines.even, lines.odd, 0, 0), 0x44332211);
    l1test::expect_eq(ctx, "unaligned word", probe.assemble(lines.even, lines.odd, 0, 2), 0x66554433);
    l1test::section("3. direct beat");
    l1test::expect_eq(ctx, "second word", probe.direct(), 0x88776655);
    return l1test::finish("L1CacheRefill_test", ctx);
}
int main() { return L1CacheRefill_test_main(); }
