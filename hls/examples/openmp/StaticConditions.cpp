#include "../../Clocked.h"

struct PipelineMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) const {
        uint32_t counter = value;
        if ((counter++ > 0) && false) counter += 1000;
        uint32_t selected = ((counter++ > 0) || true) ? 3u : 4u;
        uint32_t sum = 0;
        for (int i = 0; i < 4; ++i) {
            if (uint64_t(i) - 1 > uint64_t(0)) sum += uint32_t(i + 1);
        }
        return (uint64_t(index) << 32) | uint32_t(counter + selected + sum + operation);
    }
    uint32_t expected(uint32_t operation, uint32_t, uint32_t value) const {
        return value + 13u + operation;
    }
};
#include "Test.h"
