#include "../Allocator.h"
#include <string>
#include <vector>
#include "cpphdl_module.h"

class BoundedVector : public cpphdl::Module {
    cpphdl::hls::Arena<512> arena;
    std::vector<uint32_t, cpphdl::hls::Allocator<uint32_t, 512>> values;
public:
    BoundedVector() : values(cpphdl::hls::Allocator<uint32_t, 512>(arena)) {}

    uint64_t command(uint32_t operation, uint32_t index, uint32_t value) {
        switch (operation) {
        case 0: values.push_back(value); return values.size();
        case 1: return index < values.size() ? values[index] : uint64_t(1) << 32;
        case 2:
            if (index >= values.size()) return uint64_t(1) << 32;
            values.erase(values.begin() + index);
            return values.size();
        case 3: return values.size();
        case 4: values.clear(); return 0;
        default: return uint64_t(2) << 32;
        }
    }
};

// A transaction entry calls the actual instantiated standard-library methods.
extern "C" const uint64_t cpphdl_hls_state_bytes = sizeof(BoundedVector);
extern "C" const uint64_t cpphdl_hls_state_align = alignof(BoundedVector);
extern "C" uint64_t bounded_vector(BoundedVector* self, uint32_t operation,
                                    uint32_t index, uint32_t value) {
    if (operation == UINT32_MAX) {
        new (self) BoundedVector;
        return 0;
    }
    return self->command(operation, index, value);
}

#ifndef CPPHDL_HLS_COMPILING
#include "ScheduledTest.h"
#endif
