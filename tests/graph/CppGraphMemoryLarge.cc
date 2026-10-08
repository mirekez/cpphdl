#include "cpphdl.h"
#ifndef GRAPH_MEMORY_DEPTH
#define GRAPH_MEMORY_DEPTH 33554432
#endif
class LargeMemory : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<64>) address_in, data_in;
    _PORT(cpphdl::logic<1>) read_in, write_in;
    _PORT(cpphdl::logic<64>) result_out = _ASSIGN(read_in() ? cpphdl::logic<64>(storage[uint64_t(address_in())]) : cpphdl::logic<64>(0));
    cpphdl::memory<cpphdl::logic<64>, 1, GRAPH_MEMORY_DEPTH> storage;
    void _work(bool) { if (write_in()) storage[uint64_t(address_in())] = data_in(); }
    void _strobe() { storage.apply(); }
};
extern LargeMemory cpphdl_top;

#ifdef CPP_GRAPH_MEMORY_LARGE_RUN
#include "model.h"
#include <cstdio>
int main() {
    static_assert(sizeof(cpphdl_native::Model) < 1024);
    cpphdl_native::Model model;
    const uint64_t addresses[] = {0, GRAPH_MEMORY_DEPTH / 2, GRAPH_MEMORY_DEPTH - 1};
    model.write[0] = 1;
    for (auto address : addresses) {
        model.address[0] = address; model.address[1] = address >> 32;
        model.data[0] = address ^ 0x76543210; model.data[1] = 0x89abcdef;
        model.step();
    }
    model.write[0] = 0; model.read[0] = 1;
    for (auto address : addresses) {
        model.address[0] = address; model.address[1] = address >> 32;
        model.eval();
        if (model.result[0] != (address ^ 0x76543210) || model.result[1] != 0x89abcdef) return 1;
    }
    model.read[0] = 0; model.address[0] = GRAPH_MEMORY_DEPTH; model.eval();
    model.read[0] = 1;
    try { model.eval(); return 2; } catch (const std::out_of_range&) {}
    model.read[0] = 0; model.write[0] = 1;
    model.eval();
    try { model.step(); return 3; } catch (const std::out_of_range&) {}
    model.write[0] = 0; model.read[0] = 1; model.address[0] = 0; model.address[1] = 1;
    try { model.eval(); return 4; } catch (const std::out_of_range&) {}
    std::printf("ordinary C++ graph: %u-row SRAM endpoints, guards and untruncated bounds checks passed\n", GRAPH_MEMORY_DEPTH);
}
#endif
