#include <cpphdl.h>
using namespace cpphdl;

#ifdef CPPHDL_STATIC
#define LAZY_STORAGE inline static
#define LAZY_METHOD static
#else
#define LAZY_STORAGE
#define LAZY_METHOD
#endif

class LazyComb : public Module {
public:
    _PORT(uint32_t) input_in;
    _PORT(uint32_t) first_out;
    _PORT(uint32_t) second_out;
#ifndef SYNTHESIS
    LAZY_STORAGE unsigned evaluations = 0;
#endif
    _LAZY_COMB(value_comb, uint32_t)
#ifndef SYNTHESIS
        ++evaluations;
#endif
        value_comb = input_in() ^ 0x12345678u;
        return value_comb;
    }
    LAZY_METHOD void _assign() {
        // Separate port caches must still share one lazy evaluation per cycle.
        first_out = _ASSIGN_COMB(value_comb_func());
        second_out = _ASSIGN_COMB(value_comb_func());
    }
};

#undef LAZY_STORAGE
#undef LAZY_METHOD
