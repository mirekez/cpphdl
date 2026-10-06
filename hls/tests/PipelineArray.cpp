#include "../Clocked.h"

#define PIPELINE_CUSTOM_METHODS
struct PipelineMethods {
    uint32_t before = 17;
    uint32_t prices[4][2]{};
    uint32_t after = 29;

    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        uint32_t row = index & 3;
        uint32_t age = operation & 1;
        uint32_t old = prices[row][age];
        prices[row][age] = value;
        prices[row][1 - age] = old;
        // Dynamic read-after-write and constant reads must observe the decoded
        // write, without claiming that every row received the same value.
        return (uint64_t(prices[row][age]) << 32) |
            (prices[(row + 1) & 3][age] ^ prices[0][0] ^ old ^ before ^ after);
    }
};

#include "Pipeline.cpp"
