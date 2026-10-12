#pragma once
#define PIPELINE_CUSTOM_METHODS
#ifndef SYNTHESIS
#include <omp.h>
#define main pipeline_test_main
#endif
#include "../../tests/Pipeline.cpp"
#ifndef SYNTHESIS
#undef main
int main() {
    unsigned team = 0;
#pragma omp parallel reduction(+:team)
    team += 1;
    if (team < 2) {
        std::fprintf(stderr, "OpenMP test requires at least two worker threads\n");
        return 1;
    }
    PipelineMethods algorithm;
    std::mt19937 random(0x4f4d50);
    for (unsigned i = 0; i < 1000; ++i) {
        uint32_t operation = random(), index = random(), value = random();
        uint64_t expected = (uint64_t(index) << 32) | algorithm.expected(operation, index, value);
        if (algorithm.command(operation, index, value) != expected) {
            std::fprintf(stderr, "OpenMP software result differs from scalar reference at input %u\n", i);
            return 1;
        }
    }
    std::printf("OpenMP: %u host threads; 1000 independent scalar-reference checks passed\n", team);
    return pipeline_test_main();
}
#endif
