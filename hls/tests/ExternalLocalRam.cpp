#include "../Clocked.h"
#ifndef LOCAL_RAM_BYTES
#define LOCAL_RAM_BYTES 513
#endif
#ifndef LOCAL_RAM_BLOCK
#define LOCAL_RAM_BLOCK 1
#endif

struct ExternalLocalRamMethods {
    uint32_t prefix = 0x11223344;
    uint8_t local[LOCAL_RAM_BYTES]{};
    uint32_t suffix = 0x55667788;
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        auto external = cpphdl::hls::external_memory<uint64_t>(0x1000);
        auto words = cpphdl::hls::external_memory<uint32_t>(0x1000);
        uint64_t sum = 0;
        if (operation == 0) {
            for (uint32_t i = 0; i < LOCAL_RAM_BYTES; ++i) sum += local[i];
            return sum + prefix + suffix;
        }
        if (operation == 1) {
            local[index] = uint8_t(external[index & 7] + value);
            return local[index];
        }
        if (operation == 2) {
            words[index & 7] = uint32_t(local[index]) + value;
            return words[index & 7];
        }
        if (operation == 3) {
            local[LOCAL_RAM_BYTES - 1] = uint8_t(value);
            local[0] = uint8_t(value ^ 0xffu);
            return uint64_t(prefix) + suffix + local[0] + local[LOCAL_RAM_BYTES - 1];
        }
        uint8_t temporary[LOCAL_RAM_BYTES]{};
        temporary[LOCAL_RAM_BYTES - 1] = local[index];
        return uint64_t(temporary[0]) + temporary[LOCAL_RAM_BYTES - 1];
    }
};

class ExternalLocalRamTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedMemory<ExternalLocalRamMethods, 32, LOCAL_RAM_BLOCK> worker;
    cpphdl::hls::ExternalMemoryIf<> memory_out;
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
        assignIf(*this, worker, memory_out, worker.memory_out);
        command_ready_out = _ASSIGN(worker.command_ready_out());
        response_valid_out = _ASSIGN(worker.response_valid_out());
        result_out = _ASSIGN(worker.result_out());
        fault_out = _ASSIGN(worker.fault_out());
    }
    void _work(bool reset) { worker._work(reset); }
    void _strobe() { worker._strobe(); }
};

#ifndef SYNTHESIS
#include <cstdio>
long _system_clock = 0;
int main() {
    uint64_t memory[8] = {10,20,30,40};
    cpphdl::hls::bind_external_memory(memory, sizeof(memory), 0x1000);
    ExternalLocalRamMethods methods;
    if (methods.command(0,0,0) != 0x6688aacc || methods.command(1,17,2) != 22 ||
        methods.command(2,17,8) != 30 || methods.command(3,0,0xa5) != 0x6688abcb ||
        methods.command(4,0,0) != 90 || methods.command(4,LOCAL_RAM_BYTES-1,0) != 165 ||
        methods.command(0,0,0) != 0x6688abe1) return 1;
    methods = ExternalLocalRamMethods{};
    if (methods.command(0,0,0) != 0x6688aacc || methods.command(4,17,0)) return 1;
    std::puts("PASS: independent external memory and local buffers, zero initialization and tail bytes");
}
#endif
