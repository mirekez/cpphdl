#include <cpphdl.h>
using namespace cpphdl;

template<unsigned WIDTH = 9>
class CastWidths : public Module {
public:
    using lane_t = logic<WIDTH>;
    using unused_lane_t = logic<WIDTH + 1>;
    _PORT(logic<64>) value_in;
    _PORT(logic<64>) result_out;
    logic<64> result_comb;
    logic<64>& result_comb_func() {
        uint64_t x;
        x = value_in();
        result_comb = static_cast<uint64_t>(static_cast<lane_t>(x))
            ^ (static_cast<uint64_t>((lane_t)(x >> 7)) << 1)
            ^ (static_cast<uint64_t>(lane_t(x >> 13)) << 2);
        return result_comb;
    }
    void _assign() { result_out = _ASSIGN_COMB(result_comb_func()); }
    void _work(bool reset) {}
    void _strobe() {}
};

// Convert from an actual specialization, but retain WIDTH in emitted casts.
template class CastWidths<9>;
