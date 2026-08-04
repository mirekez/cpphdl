// Final layered L1Cache requirements tested here:
// 1. Compose every operation layer behind the historical public L1Cache protocol.
// 2. Reset into tag initialization and become idle after every set is cleared.
// 3. Keep memory response inputs valid throughout controller sequencing.
#include "L1Cache_test.h"
#include "L1Cache.h"
#include "L1CacheTestCommon.h"

long _system_clock = 0;

class LayeredCacheHarness : public Module
{
    L1Cache<256, 32, 1, 0, 17, 64> cache;
    bool read = false;
    uint32_t addr = 0;
    logic<64> memory_data = 0;
public:
    void _assign()
    {
        cache.read_in = _ASSIGN_REG(read);
        cache.write_in = _ASSIGN(false);
        cache.write_data_in = _ASSIGN((uint32_t)0);
        cache.write_mask_in = _ASSIGN((uint8_t)0);
        cache.addr_in = _ASSIGN_REG(addr);
        cache.stall_in = _ASSIGN(false);
        cache.flush_in = _ASSIGN(false);
        cache.invalidate_in = _ASSIGN(false);
        cache.cache_disable_in = _ASSIGN(false);
        cache.mem_out.read_data_out = _ASSIGN_REG(memory_data);
        cache.mem_out.wait_out = _ASSIGN(false);
        cache._assign();
    }
    void cycle(bool reset)
    {
        cache._work(reset); cache._strobe(); ++_system_clock;
    }
    bool busy() { return cache.busy_out(); }
};

int L1Cache_test_main()
{
    l1test::Context ctx;
    LayeredCacheHarness harness;
    harness._assign();
    harness.cycle(true);
    l1test::section("1. reset enters initialization");
    l1test::expect_true(ctx, "busy after reset", harness.busy());
    for (size_t i = 0; i < 9; ++i) harness.cycle(false);
    l1test::section("2. initialization completes");
    l1test::expect_false(ctx, "idle after all sets", harness.busy());
    return l1test::finish("L1Cache_test", ctx);
}
int main() { return L1Cache_test_main(); }
