// L1CacheRequest requirements tested here:
// 1. Decode one registered address into set/tag/word/refill-beat properties.
// 2. Classify live cacheable and bypass requests without mutating state.
// 3. Build one grouped backing-memory driver for refill and CPU writes.
#include "L1CacheRequest_test.h"
#include "L1CacheRequest.h"
#include "L1CacheTestCommon.h"

long _system_clock = 0;

class RequestProbe : public L1CacheRequest<1024, 32, 2, 1, 17, 64>
{
    uint32_t input_addr = 0;
    bool input_disable = false;
    bool input_write = false;
    uint32_t input_write_data = 0;
    uint8_t input_write_mask = 0;
public:
    void _assign()
    {
        addr_in = _ASSIGN_REG(input_addr);
        cache_disable_in = _ASSIGN_REG(input_disable);
        write_in = _ASSIGN_REG(input_write);
        write_data_in = _ASSIGN_REG(input_write_data);
        write_mask_in = _ASSIGN_REG(input_write_mask);
    }
    void set_input(uint32_t addr, bool disable, bool write = false)
    {
        input_addr = addr; input_disable = disable; input_write = write; ++_system_clock;
    }
    void set_request(uint32_t addr, bool cacheable, bool disabled, uint32_t state, uint32_t beat)
    {
        req_reg._next.addr = addr; req_reg._next.cacheable = cacheable;
        req_reg._next.cache_disable = disabled; req_reg._next.read = true;
        state_reg._next = state; refill_reg._next.beat = beat;
        req_reg.strobe(); state_reg.strobe(); refill_reg.strobe(); ++_system_clock;
    }
    L1RequestGeometryComb geometry() { return request_geometry_comb_func(); }
    L1InputRequestComb input() { return input_decode_comb_func(); }
    L1MemDriver driver() { return mem_driver_comb_func(); }
};

int L1CacheRequest_test_main()
{
    l1test::Context ctx;
    RequestProbe probe;
    probe._assign();
    probe.set_request(0x13c, true, false, L1_ST_REFILL, 2);
    l1test::section("1. registered decode");
    auto geometry = probe.geometry();
    l1test::expect_eq(ctx, "set", geometry.set, 9);
    l1test::expect_eq(ctx, "word", geometry.word, 7);
    l1test::expect_eq(ctx, "refill beat", geometry.refill_beat, 3);
    l1test::section("2. live decode");
    probe.set_input(0x13d, false);
    l1test::expect_false(ctx, "D crossing bypass", probe.input().cacheable);
    probe.set_input(0x138, false);
    l1test::expect_true(ctx, "aligned cacheable", probe.input().cacheable);
    l1test::section("3. grouped memory driver");
    auto driver = probe.driver();
    l1test::expect_true(ctx, "refill read", driver.read);
    l1test::expect_eq(ctx, "refill address", driver.addr, 0x130);
    return l1test::finish("L1CacheRequest_test", ctx);
}
int main() { return L1CacheRequest_test_main(); }
