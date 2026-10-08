#include "cpphdl.h"

#ifndef GRAPH_CONSTRUCTION_STAGES
#define GRAPH_CONSTRUCTION_STAGES 4096
#endif

class ConstructionProbe : public cpphdl::Module {
public:
    using Word = cpphdl::logic<64>;
    _PORT(Word) data_in;
    _PORT(cpphdl::logic<1>) enable_in;
    _PORT(Word) result_out = _ASSIGN(transform());

    Word scopedCopy(Word data) {
        Word result = data;
        for (unsigned index = 0; index < 4; ++index) {
            cpphdl::array<2, Word> copies;
            copies[0] = result;
            copies[1] = result;
            auto& outer = result;
            {
                // Shadowing must not lose the reference to the outer result.
                Word result = copies[index % 2];
                outer = result;
            }
            if (uint64_t(enable_in())) {
                Word returned = result;
                return returned;
            }
        }
        return result;
    }

    Word transform() {
        Word data = scopedCopy(data_in());
        for (unsigned index = 0; index < GRAPH_CONSTRUCTION_STAGES; ++index) {
            const uint64_t mask = uint64_t(1) << (index % 64);
            Word field = uint64_t(data) & mask;
            Word rest = uint64_t(data) & ~mask;
            Word merged = uint64_t(rest) | uint64_t(field);
            data = uint64_t(enable_in()) ? merged : data;
            data = uint64_t(data) | uint64_t(0);
        }
        return data;
    }
};

extern ConstructionProbe cpphdl_top;
