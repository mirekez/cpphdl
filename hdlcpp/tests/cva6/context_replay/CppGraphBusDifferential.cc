#include "model.h"
#include "VXbarBench.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    uint64_t randomState = std::strtoull(argv[1], nullptr, 0);
    auto randomWord = [&]() {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 7;
        randomState ^= randomState << 17;
        return uint32_t(randomState);
    };
    cpphdl_native::Model model;
    VXbarBench reference;
    for (unsigned cycle = 0; cycle < 20000; ++cycle) {
        model.rst_ni[0] = reference.rst_ni = cycle > 2 && randomWord() % 97 != 0;
        model.work_reset[0] = !model.rst_ni[0];
        for (unsigned word = 0; word < 24; ++word) model.requests[word] = reference.requests[word] = randomWord();
        for (unsigned word = 0; word < 47; ++word) model.responses[word] = reference.responses[word] = randomWord();
        model.requests[23] &= 4095; reference.requests[23] &= 4095;
        model.responses[46] &= 255; reference.responses[46] &= 255;
        for (unsigned phase = 0; phase < 2; ++phase) {
            reference.clk_i = phase;
            reference.eval();
            if (phase || !model.rst_ni[0]) model.step();
            else model.eval();
            for (unsigned word = 0; word < 10; ++word) {
                if (model.slave_outputs[word] != reference.slave_outputs[word]) {
                    std::fprintf(stderr, "slave mismatch cycle=%u phase=%u word=%u cpp=%08x reference=%08x\n",
                                 cycle, phase, word, model.slave_outputs[word], reference.slave_outputs[word]);
                    return 1;
                }
            }
            for (unsigned word = 0; word < 118; ++word) {
                if (model.master_outputs[word] != reference.master_outputs[word]) {
                    std::fprintf(stderr, "master mismatch cycle=%u phase=%u word=%u cpp=%08x reference=%08x\n",
                                 cycle, phase, word, model.master_outputs[word], reference.master_outputs[word]);
                    return 1;
                }
            }
        }
    }
    std::puts("ordinary C++ graph: 20000 random full-bus cycles, both phases match original-SV Verilator");
}
