#include "../Clocked.h"
#include "ClockedOptions.h"

struct SharingMethods {
    uint32_t data[8]{};
    static constexpr bool singleClock(uint32_t op) { return op != 1 && op != 2; }
    static void mutate(uint32_t* first, uint32_t* second, uint32_t bias) {
        *first += bias;
        *second ^= *first + 3;
    }
    static void conditional(uint32_t* target, uint32_t value, bool enable) {
        if (!enable) return;
        *target += value;
    }
    template<class T> static void typed(T* target, T value) { *target += value; }
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        uint32_t i = index % 8;
        if (operation == 0) { data[i] = value; return data[i]; }
        if (operation == 1) {
            uint64_t sum = value;
            for (uint32_t n = 0; n < 8; ++n) sum += data[n];
            return sum;
        }
        if (operation == 2) {
            for (uint32_t n = 0; n < 8; ++n) data[n] = value;
            return data[0];
        }
        if (operation == 4) {
            uint32_t next = (i + 1) % 8;
            uint32_t saved = data[i];
            mutate(&data[i], &data[next], value);
            mutate(&data[next], &data[i], value + 7);
            mutate(&data[i], &data[i], value + 13);
            return (uint64_t(saved) << 32) | (data[i] ^ data[next]);
        }
        if (operation == 5) {
            conditional(&data[i], value, index & 1);
            conditional(&data[i], value + 3, index & 2);
            conditional(nullptr, value, false);
            return data[i];
        }
        if (operation == 6) {
            uint16_t short_value = uint16_t(value);
            uint32_t long_value = value;
            typed(&short_value, uint16_t(index));
            typed(&long_value, index);
            return (uint64_t(long_value) << 32) | short_value;
        }
        return data[i];
    }
#ifndef SYNTHESIS
    template<class Test> static void extraTests(Test&& test) {
        for (uint32_t n = 0; n < 16; ++n) {
            test(4, n, 0x76543210u + n);
            test(5, n, 0x12345678u + n);
            test(6, n + 65530, 0xfffffff0u + n);
            test(1, n, 17);
        }
    }
#endif
};

class ClockedSharingTop : public cpphdl::Module {
public:
    cpphdl::hls::Clocked<SharingMethods, 0, 16, 4096, HLS_SHARED_MEMORY, HLS_BLOCK_RAM> worker;
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
#include "ClockedTest.h"
int main() {
    int result = clockedTest<SharingMethods, ClockedSharingTop>();
#ifdef VERILATOR
    if (result) return result;
    for (unsigned elapsed = 1; elapsed <= 12; ++elapsed) {
        VClockedModel dut;
        auto tick = [&] { dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval(); };
        dut.command_valid_in = 0; dut.response_ready_in = 0;
        dut.operation_in = 0; dut.index_in = 0; dut.value_in = 0;
        dut.reset = 1; tick(); dut.reset = 0;
        for (unsigned n = 0; !dut.command_ready_out && n < 1000; ++n) tick();
        if (!dut.command_ready_out) return 1;
        dut.operation_in = 4; dut.value_in = 27; dut.command_valid_in = 1;
        tick(); dut.command_valid_in = 0;
        for (unsigned n = 1; n < elapsed; ++n) tick();
        dut.reset = 1; tick(); dut.reset = 0;
        for (unsigned n = 0; !dut.command_ready_out && n < 1000; ++n) tick();
        if (!dut.command_ready_out || dut.response_valid_out || dut.fault_out) return 1;
        dut.operation_in = 3; dut.command_valid_in = 1;
        tick(); dut.command_valid_in = 0;
        for (unsigned n = 0; !dut.response_valid_out && n < 1000; ++n) tick();
        if (!dut.response_valid_out || dut.result_out != 0 || dut.fault_out) return 1;
    }
    std::puts("reset during shared calls passed");
#endif
    return result;
}
#endif
