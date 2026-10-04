#include "../Clocked.h"

// A 96-bit object exercises state splitting as well as conditional updates.
struct PipelineMethods {
    uint32_t counter = 7;
    uint32_t phase = 3;
    uint32_t history = 0x12345678u;
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        uint32_t previous = counter;
        if (operation & 1) counter += value ^ history;
        else counter = (counter + index) ^ (value << 3);
        if (operation & 2) history = previous + value;
        phase = (phase + 1) & 7;
        return (uint64_t(index) << 32) | (counter ^ history ^ phase);
    }
};
#define PIPELINE_CUSTOM_METHODS
#include "Pipeline.cpp"
