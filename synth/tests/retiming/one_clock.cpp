#include "cpphdl.h"

class FunctionBoundary : public cpphdl::Module {
public:
    _PORT(uint8_t) a_in;
    _PORT(uint8_t) result_out = _ASSIGN_REG(result);
    cpphdl::reg<cpphdl::u8> result;
    static uint8_t bias(uint8_t value) { return uint8_t(value + 13); }
    [[clang::annotate("CPPHDL_ONE_CLOCK")]]
    uint8_t transform(uint8_t value) {
#ifdef BAD_EFFECT
        result._next = value;
#endif
        value = bias(value);
        value ^= 0x59;
#ifdef TOO_LONG
        value = uint8_t(value + 29);
        value ^= uint8_t(value << 1);
        value = uint8_t(value + 31);
#endif
        return value;
    }
    void _work(bool reset) {
        uint8_t x;
        x = transform(a_in());
        x = uint8_t(x + 7);
        result._next = transform(x);
        if (reset) result._next = 0;
    }
    void _strobe() { result.strobe(); }
};
FunctionBoundary cpphdl_top;
#ifdef RETIMING_RUN
#include "OneClockTest.h"
#endif
