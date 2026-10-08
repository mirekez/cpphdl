#include "../Clocked.h"
#include "DelayedOptions.h"

struct MemoryEffectsMethods {
    struct Wide { uint64_t a, b; };
    uint32_t data[8]{};
    Wide wide[2]{};
    static constexpr bool singleClock(uint32_t op) { return op != 1 && op != 2 && op != 7 && op != 12 && op != 13; }
    static uint32_t get(const uint32_t* p) { return *p; }
    static void put(uint32_t* p, uint32_t value) { *p = value; }
    void indirect_write(uint32_t* p, uint32_t value) const { *p = value; }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        uint32_t i = index % 8;
        uint32_t* p = &data[i];
        if (operation == 0) { *p = value; return *p; }
        if (operation == 1) {
            uint64_t sum = value;
            for (uint32_t n = 0; n < 8; ++n) sum += data[n];
            return sum;
        }
        if (operation == 2) {
            for (uint32_t n = 0; n < 8; ++n) data[n] = value;
            return data[0];
        }
        if (operation == 3) return uint64_t(get(p)) + get(p);
        if (operation == 4) {
            uint64_t first = get(p);
            if (value & 1) first += get(p);
            else first ^= get(p);
            return first + get(p);
        }
        if (operation == 5) {
            uint32_t before = *p;
            uint32_t* q = &data[(index & 1) ? i : (i + 1) % 8];
            put(q, value);
            return (uint64_t(before) << 32) | *p;
        }
        if (operation == 6) {
            uint32_t before = *p;
            reinterpret_cast<unsigned char*>(p)[1] = uint8_t(value);
            return (uint64_t(before) << 32) | *p;
        }
        if (operation == 7) {
            uint64_t sum = 0;
            for (uint32_t n = 0; n < 4; ++n) { sum += get(p); *p += value; }
            return sum + *p;
        }
        if (operation == 8) {
            uint32_t before = *p;
            p = &data[(i + 1) % 8];
            return (uint64_t(before) << 32) | *p;
        }
        if (operation == 9) {
            uint32_t before = *p;
            if (value & 1) *p = value;
            return (uint64_t(before) << 32) | *p;
        }
        if (operation == 10) {
            // The other memory request is a clock boundary in port mode.
            uint32_t before = *p;
            uint32_t other = data[(i + 1) % 8];
            return uint64_t(before) + other + *p;
        }
        if (operation == 11) {
            const uint32_t* q = (value & 1) ? p : nullptr;
            if (q) return uint64_t(get(q)) + get(q);
            return value;
        }
        if (operation == 12) {
            uint64_t sum = 0;
            for (uint32_t n = 0; n < 4; ++n) sum += get(p);
            return sum;
        }
        if (operation == 13) {
            uint32_t sample = get(p);
            uint64_t sum = 0;
            for (uint32_t n = 0; n < 4; ++n) sum += sample;
            return sum;
        }
        if (operation == 14) {
            // Invalid commands are exercised only by RTL's bounds-fault test.
            const uint32_t* invalid = reinterpret_cast<const uint32_t*>(uintptr_t(index));
            return uint64_t(get(invalid)) + get(invalid);
        }
        if (operation == 15) {
            uint32_t before = get(p);
            indirect_write(p, value);
            return (uint64_t(before) << 32) | get(p);
        }
        if (operation == 16) {
            Wide first = wide[index % 2];
            Wide second = wide[index % 2];
            return first.a + second.b;
        }
        if (operation == 17) {
            wide[index % 2] = Wide{value, index};
            return value;
        }
        return *p;
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        for (uint32_t i = 0; i < 16; ++i) {
            for (uint32_t op = 3; op <= 13; ++op) test(op, i, 0xabcdef00u + i);
            test(15, i, 0x76543210u + i);
            test(17, i, 0x12345678u + i);
            test(16, i, 0);
            test(1, i, 17);
        }
    }
#endif
};

class DelayedMemoryEffectsTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedDelayer<MemoryEffectsMethods, 0, 16, 4096, HLS_SHARED_MEMORY, HLS_BLOCK_RAM> worker;
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
    void _assign() {
        worker.command_valid_in = _ASSIGN(command_valid_in());
        worker.operation_in = _ASSIGN(operation_in());
        worker.index_in = _ASSIGN(index_in());
        worker.value_in = _ASSIGN(value_in());
        worker.response_ready_in = _ASSIGN(response_ready_in());
        worker._assign();
        command_ready_out = _ASSIGN(worker.command_ready_out());
        response_valid_out = _ASSIGN(worker.response_valid_out());
        result_out = _ASSIGN(worker.result_out());
        fault_out = _ASSIGN(worker.fault_out());
    }
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};

#ifndef SYNTHESIS
#include "DelayedTest.h"
int main() {
    int result = delayedTest<MemoryEffectsMethods, DelayedMemoryEffectsTop>();
#ifdef VERILATOR
    if (result) return result;
    VDelayedModel dut;
    auto tick = [&] { dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval(); };
    dut.command_valid_in = 0; dut.response_ready_in = 0;
    dut.operation_in = 0; dut.index_in = 0; dut.value_in = 0;
    dut.reset = 1; tick(); dut.reset = 0;
    for (unsigned n = 0; !dut.command_ready_out && n < 1000; ++n) tick();
    if (!dut.command_ready_out) return 1;
    unsigned loopClocks = 0;
    for (unsigned op : {3u, 4u, 10u, 11u, 12u, 13u, 16u}) {
        for (unsigned value : {0u, 1u}) {
            dut.operation_in = op; dut.value_in = value; dut.command_valid_in = 1;
            tick(); dut.command_valid_in = 0;
            unsigned clocks = 1;
            for (; !dut.response_valid_out && clocks < 1000; ++clocks) tick();
            unsigned expected = !HLS_SHARED_MEMORY || (op == 11 && !value) ? 1 :
                op == 16 ? 5 : op == 10 ? 4 : 2;
            if (op == 12) { loopClocks = clocks; expected = clocks; }
            if (op == 13) expected = loopClocks - (HLS_SHARED_MEMORY ? 3 : 0);
            if (dut.fault_out || !dut.response_valid_out || dut.result_out || clocks != expected) {
                std::fprintf(stderr, "memory reuse op=%u branch=%u clocks=%u expected=%u fault=%u\n",
                    op, value, clocks, expected, dut.fault_out);
                return 1;
            }
            dut.response_ready_in = 1; tick(); dut.response_ready_in = 0;
        }
    }
    std::puts("read reuse, both branch paths, null guards, loop and memory clock boundaries passed");
    for (unsigned invalid : {0u, 1u, 65535u}) {
        dut.reset = 1; tick(); dut.reset = 0;
        for (unsigned n = 0; !dut.command_ready_out && n < 1000; ++n) tick();
        if (!dut.command_ready_out) return 1;
        dut.operation_in = 14; dut.index_in = invalid; dut.command_valid_in = 1;
        tick(); dut.command_valid_in = 0;
        for (unsigned n = 0; !dut.response_valid_out && n < 1000; ++n) tick();
        if (!dut.response_valid_out || dut.fault_out != 3) {
            std::fprintf(stderr, "missing bounds fault for repeated read at %u: %u\n", invalid, dut.fault_out);
            return 1;
        }
    }
    std::puts("reused reads preserve null and out-of-range faults");
#endif
    return result;
}
#endif
