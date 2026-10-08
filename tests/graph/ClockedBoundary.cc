#include "../../hls/Clocked.h"

struct Transaction {
    uint32_t count = 0;
    uint64_t command(uint32_t, uint32_t, uint32_t value) {
        for (uint32_t i = 0; i < value; ++i) ++count;
        return count;
    }
};
cpphdl::hls::Clocked<Transaction> cpphdl_top;
