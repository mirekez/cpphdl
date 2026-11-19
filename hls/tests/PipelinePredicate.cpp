#include "../Clocked.h"

// Pure, single-definition temporaries are nested under command predicates.
// State updates and returned values still depend on the active branch.
struct PipelineMethods {
    uint32_t history = 11;
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        uint32_t answer = 0;
        if (operation & 1) {
            uint32_t first = value + index * 3;
            if ((operation & 2) && (value & 1)) {
                history ^= first;
                return (uint64_t(index) << 32) | (first + history);
            }
            uint32_t second = first * 7;
            if (operation & 4) {
                // The increment must remain conditional on BOTH predicates.
                if ((operation & 8) && (++history & 1)) answer = second ^ history;
                else answer = second + 1;
            } else answer = second - 1;
        } else if (operation & 16) {
            uint32_t other = (value ^ index) + 9;
            if (operation & 64) history += other;
            answer = other + history;
        } else answer = value ^ 0x5a5a5a5au;
        if (operation & 32) history += answer;
        return (uint64_t(index) << 32) | answer;
    }
};
#define PIPELINE_CUSTOM_METHODS
#include "Pipeline.cpp"
