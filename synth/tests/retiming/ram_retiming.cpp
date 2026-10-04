#include "cpphdl.h"
class RamRetiming : public cpphdl::Module {
public:
    _PORT(uint8_t) address_in;
    _PORT(uint16_t) data_in;
    _PORT(uint16_t) bias_in;
    _PORT(bool) write_in;
    _PORT(bool) read_in;
    _PORT(uint8_t) tag_value_in;
    _PORT(uint16_t) result_out = _ASSIGN_REG(result);
    _PORT(uint8_t) tag_out = _ASSIGN_REG(tag);
    cpphdl::memory<cpphdl::logic<16>, 1, 8> ram;
    cpphdl::reg<cpphdl::logic<16>> result;
    cpphdl::reg<cpphdl::logic<8>> tag;
    void _work(bool reset) {
        uint16_t x;
        result._next = 0;
        if (read_in()) {
            x = ram[address_in()].to_ullong();
            x = x + bias_in();
            x = x ^ 0x1379;
            x = x + 23;
            x = x ^ (x << 1);
            result._next = x + 41;
        }
        if (write_in() && !reset) ram[address_in()] = data_in();
        tag._next = tag_value_in();
        if (reset) { result._next = 0; tag._next = 0x5a; }
    }
    void _strobe() { result.strobe(); tag.strobe(); ram.apply(); }
};
RamRetiming cpphdl_top;
#ifdef RETIMING_RUN
#define RAM_RETIMING
#include "Test.h"
#endif
