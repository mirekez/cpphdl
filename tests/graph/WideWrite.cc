#include <cpphdl.h>
using namespace cpphdl;
#ifndef WRITE_LINE
#define WRITE_LINE 128
#endif
#ifndef WRITE_SLICE
#define WRITE_SLICE 64
#endif
#ifndef WRITE_DATA
#define WRITE_DATA 64
#endif
#ifndef WRITE_ALIGNED
#define WRITE_ALIGNED 1
#endif

template<unsigned LineWidth = 128, unsigned SliceWidth = 64,
         unsigned DataWidth = 64, bool Aligned = true>
class WideWrite : public Module {
public:
    _PORT(logic<1>) word_in, enable_in, overlap_in;
    _PORT(logic<16>) first_in, second_in;
    _PORT(logic<DataWidth>) data_in, data2_in;
    _PORT(logic<LineWidth>) result_out;
    _PORT(logic<SliceWidth>) observed_out;
    reg<logic<LineWidth>> buffer;
    reg<logic<SliceWidth>> observed;

    void _assign() {
        result_out = _ASSIGN_REG(buffer);
        observed_out = _ASSIGN_REG(observed);
    }
    void _work(bool reset) {
        const uint64_t low = Aligned ? uint64_t(word_in()) * 64 : uint64_t(first_in());
        const uint64_t second = Aligned ? (1 - uint64_t(word_in())) * 64 : uint64_t(second_in());
        buffer._next = buffer;
        if (enable_in()) {
            buffer._next.bits(low + SliceWidth - 1, low) = data_in();
            if (overlap_in()) buffer._next.bits(second + SliceWidth - 1, second) = data2_in();
        }
        // A read after consecutive writes must see their final merged value.
        observed._next = buffer._next.bits(low + SliceWidth - 1, low);
        if (reset) {
            buffer._next = 0;
            observed._next = 0;
        }
    }
    void _strobe() {
        buffer.strobe();
        observed.strobe();
    }
};
template class WideWrite<WRITE_LINE, WRITE_SLICE, WRITE_DATA, WRITE_ALIGNED>;
extern WideWrite<WRITE_LINE, WRITE_SLICE, WRITE_DATA, WRITE_ALIGNED> cpphdl_top;
