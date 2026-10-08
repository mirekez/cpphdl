#include "cpphdl.h"

class NegateProbe : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<8>) data_in;
    _PORT(cpphdl::logic<8>) value_out;
    _PORT(cpphdl::logic<8>) right_in;
    _PORT(cpphdl::logic<64>) wide_in;
    _PORT(cpphdl::logic<5>) mode_in;
    _PORT(cpphdl::logic<64>) result64_out;
    cpphdl::logic<8> next_comb;
    cpphdl::logic<64> wide_comb;

    void _assign() {
        value_out = _ASSIGN_COMB(next_comb_func());
        result64_out = _ASSIGN_COMB(wide_comb_func());
    }

    cpphdl::logic<64>& wide_comb_func() {
        unsigned calls = 0;
        uint64_t negated = 0;
        switch (uint64_t(mode_in())) {
        case 0: wide_comb = -cpphdl::cat{cpphdl::logic<1>(0), data_in()}; break;
        case 1: wide_comb = -cpphdl::cat{cpphdl::logic<1>(1), data_in()}; break;
        case 2:
            wide_comb = -cpphdl::cat{cpphdl::logic<32>(uint64_t(wide_in()) >> 32),
                                    cpphdl::logic<32>(wide_in())};
            break;
        case 3: wide_comb = cpphdl::cat{cpphdl::logic<1>(0), data_in()} - uint64_t(right_in()); break;
        case 4: wide_comb = uint64_t(right_in()) - cpphdl::cat{cpphdl::logic<1>(0), data_in()}; break;
        case 5: wide_comb = cpphdl::cat{cpphdl::logic<1>(0), data_in()} - cpphdl::cat{right_in()}; break;
        case 6: wide_comb = -data_in(); break;
        case 7: wide_comb = +data_in(); break;
        case 8: wide_comb = ~data_in(); break;
        case 9: wide_comb = !data_in(); break;
        case 10: wide_comb = -(-cpphdl::cat{cpphdl::logic<1>(1), data_in()}); break;
        case 11:
            negated = -cpphdl::cat{cpphdl::logic<1>((calls = calls + 1)), data_in()};
            wide_comb = (uint64_t(calls) << 56) | (negated & 0x00ffffffffffffffULL);
            break;
        case 12: wide_comb = cpphdl::logic<8>(data_in() - right_in()); break;
        case 13: wide_comb = cpphdl::logic<8>(data_in() + right_in()); break;
        case 14: wide_comb = ~cpphdl::cat{cpphdl::logic<1>(0), data_in()}; break;
        // A narrow, self-sized result must still negate at uint64_t width
        // before shifting; truncating the concatenation first loses these bits.
        case 16:
            wide_comb = cpphdl::cat{cpphdl::logic<8>((-cpphdl::cat{cpphdl::logic<1>(0), data_in()}) >> 8), cpphdl::logic<8>(0)};
            break;
        case 17:
            wide_comb = cpphdl::logic<8>((-cpphdl::cat{cpphdl::logic<1>(0), data_in()}) >> 63);
            break;
        default: wide_comb = !cpphdl::cat{cpphdl::logic<1>(0), data_in()}; break;
        }
        return wide_comb;
    }

    cpphdl::logic<8>& next_comb_func() {
#ifdef BUILTIN_NEGATION
        next_comb = -static_cast<uint64_t>(cpphdl::cat{cpphdl::logic<1>(0), data_in()});
#else
        next_comb = -cpphdl::cat{cpphdl::logic<1>(0), data_in()};
#endif
        return next_comb;
    }
};

extern NegateProbe cpphdl_top;
