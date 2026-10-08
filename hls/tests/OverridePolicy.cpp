#include "../Clocked.h"
#include "std/UnorderedMapOverrides.h"
struct OverridePolicyMethods {
    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        if (operation == 0) return unordered_map_rtl::multiply(index, value);
        if (operation == 1) return unordered_map_rtl::divide(index, value);
        if (operation == 2) return unordered_map_rtl::from_size(index);
        return unordered_map_rtl::next_prime(index);
    }
};
class OverridePolicyTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedDelayer<OverridePolicyMethods, 0, 32, 64, true> worker;
};
