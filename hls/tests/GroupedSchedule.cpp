#include <cstdint>
#include <cstring>
#include <new>

struct GroupedSchedule {
    unsigned char bytes[16] = {};

    uint64_t command(uint32_t op, uint32_t count, uint32_t value) {
        uint32_t a = value, b = value ^ 0x9e3779b9u;
        switch (op) {
        case 0:
            a = (a + count) ^ (b >> 3);
            if (a & 1) b = (a << 2) + count;
            else b = (a >> 2) ^ count;
            return (uint64_t(a + b) << 32) | (a ^ b);
        case 1:
            // The second store overlaps the first; both subsequent loads must
            // observe the bytes just written, even in a single register cycle.
            std::memcpy(bytes, &value, 4);
            std::memcpy(bytes + (count & 3), &count, 4);
            std::memcpy(&a, bytes, 4);
            std::memcpy(&b, bytes + (count & 3), 4);
            return (uint64_t(a) << 32) | b;
        case 2:
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t previous = a;
                a = b;
                b = (previous + i) ^ (b << 1);
            }
            return (uint64_t(a) << 32) | b;
        case 3:
            for (uint32_t i = 0; i < count; ++i) {
                if ((i ^ value) % 5 == 0) continue;
                if (i == 13) break;
                a = (a << 1) ^ b;
                b += a + i;
                if (b == 0) return a;
            }
            return (uint64_t(a) << 32) | b;
        case 4:
            for (uint32_t i = 0; i < count; ++i) {
                for (uint32_t j = 0; j < (value & 3); ++j) {
                    a = (a + j) ^ b;
                    b = (b << 1) + i + a;
                }
            }
            return (uint64_t(a) << 32) | b;
        default: return 0;
        }
    }
};

extern "C" const uint64_t cpphdl_hls_state_bytes = sizeof(GroupedSchedule);
extern "C" const uint64_t cpphdl_hls_state_align = alignof(GroupedSchedule);
extern "C" uint64_t grouped_schedule(GroupedSchedule* self, uint32_t op, uint32_t count, uint32_t value) {
    if (op == UINT32_MAX) { new (self) GroupedSchedule; return 0; }
    return self->command(op, count, value);
}

#ifndef CPPHDL_HLS_COMPILING
#include "ScheduledBench.h"
int main() {
    GroupedSchedule native;
    require(grouped_schedule(&native, 2, 3, 7) == 0xe6ea9f49f1bbcdefULL, "loop recurrence reference");
    require(grouped_schedule(&native, 1, 1, 0xaabbccddu) == 0x000001dd00000001ULL, "overlapping bytes reference");
#ifdef VERILATOR
    Bench bench; bench._assign(); bench.reset();
#endif
    for (uint32_t op = 0; op < 5; ++op) {
        for (uint32_t count = 0; count < 24; ++count) {
            for (uint32_t value : {0u, 1u, 2u, 3u, 0x91823u, 0xffffffffu}) {
                uint64_t expected = grouped_schedule(&native, op, count, value);
#ifdef VERILATOR
                unsigned latency = bench.transaction(op, count, value, expected, 0);
                if (op == 0 || (op == 1 && !HLS_SRAM_TEST))
                    require(latency == 1, "acyclic method must finish on its acceptance edge");
                if (op == 2)
                    require(latency == (count ? count : 1), "loop body must take one cycle per iteration");
                if (op == 3)
                    require(latency <= count + 1, "branches/continue must not add instruction cycles");
                if (op == 4)
                    require(latency <= count * ((value & 3) + 1) + 1, "nested loops have instruction-sized states");
#else
                (void)expected;
#endif
            }
        }
    }
#ifdef VERILATOR
    // Reset while a loop is suspended, not after an already completed method.
    bench.model.rtl.command_valid_in = 1; bench.model.rtl.operation_in = 2;
    bench.model.rtl.index_in = 100; bench.model.rtl.value_in = 17;
    bench.tick(); bench.model.rtl.command_valid_in = 0;
    for (unsigned i = 0; i < 3; ++i) bench.tick();
    require(!bench.model.rtl.response_valid_out, "reset regression must interrupt active work");
    bench.reset();
    bench.transaction(2, 5, 99, grouped_schedule(&native, 2, 5, 99), 0);
#endif
    std::puts("PASS grouped schedule: branches, aliases, loop PHIs, continue/break, nested loops, reset");
}
#endif
