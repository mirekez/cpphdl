#include "ScheduledBench.h"

int main() {
    auto native = std::make_unique<BoundedVector>();
    std::vector<uint32_t> oracle;
    std::mt19937 random(0x319ab);
#ifdef VERILATOR
    Bench bench; bench._assign(); bench.reset();
#endif
    auto reset = [&] {
        native = std::make_unique<BoundedVector>(); oracle.clear();
#ifdef VERILATOR
        bench.reset();
#endif
    };
    auto run = [&](uint32_t op, uint32_t index, uint32_t data) {
        uint64_t actual = 0, expected = 0; uint32_t fault = 0;
        try { actual = bounded_vector(native.get(), op, index, data); }
        catch (const std::bad_alloc&) { fault = 1; }
        catch (const std::length_error&) { fault = 1; }
        if (!fault) {
            switch (op) {
            case 0: oracle.push_back(data); expected = oracle.size(); break;
            case 1: expected = index < oracle.size() ? oracle[index] : uint64_t(1) << 32; break;
            case 2:
                if (index >= oracle.size()) expected = uint64_t(1) << 32;
                else { oracle.erase(oracle.begin() + index); expected = oracle.size(); }
                break;
            case 3: expected = oracle.size(); break;
            case 4: oracle.clear(); break;
            default: expected = uint64_t(2) << 32;
            }
            require(actual == expected, "bounded native container differs from std allocator reference");
        }
#ifdef VERILATOR
        unsigned latency = bench.transaction(op, index, data, actual, fault);
        if (!HLS_SRAM_TEST && !fault && (op == 1 || op == 3 || op == 4))
            require(latency == 1, "vector read/size/clear must not be instruction-clocked");
#endif
        return fault;
    };
    for (unsigned pass = 0; pass < 3; ++pass) {
        for (unsigned n = 0; n < 24; ++n) require(!run(0, 0, random()), "premature pool exhaustion");
        for (unsigned n = 0; n < 24; ++n) run(1, n, 0);
        for (unsigned n = 0; n < 12; ++n) run(2, 1, 0);
        for (unsigned n = 0; n < oracle.size(); ++n) run(1, n, 0);
        run(1, 999, 0); run(2, 999, 0); run(3, 0, 0); run(4, 0, 0);
        for (unsigned n = 0; n < 100; ++n) {
            uint32_t op = random() % 5;
            require(!run(op, random() % 16, random()), "unexpected random pool exhaustion");
        }
        reset();
    }
    bool exhausted = false;
    for (unsigned n = 0; n < 160; ++n) {
        if (run(0, 0, random())) { exhausted = true; break; }
    }
    require(exhausted, "bounded allocator did not report exhaustion");
    reset(); run(0, 0, 0xfeedabcd); run(1, 0, 0);
#ifdef VERILATOR
    // Abort an operation before completion, including a delayed SRAM request.
    bench.model.rtl.command_valid_in = 1; bench.model.rtl.operation_in = 0;
    bench.model.rtl.value_in = 0x98765432; bench.tick();
    bench.model.rtl.command_valid_in = 0;
    require(!bench.model.rtl.response_valid_out, "reset must interrupt an active vector reallocation");
    reset(); run(0, 0, 0x13572468); run(1, 0, 0);
    require((bench.ram.transactions != 0) == bool(HLS_SRAM_TEST), "wrong memory backend");
    std::printf("PASS actual STL vector RTL: %ld clocks, %llu SRAM transactions\n",
                _system_clock, (unsigned long long)bench.ram.transactions);
#else
    std::puts("PASS actual STL vector native: reallocation, erase, reuse, exhaustion, reset");
#endif
}
