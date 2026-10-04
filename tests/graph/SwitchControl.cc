#include <cpphdl.h>
using namespace cpphdl;

class SwitchControl : public Module {
public:
    _PORT(logic<128>) data_in;
    _PORT(uint32_t) selector_in;
    _PORT(logic<64>) result_out;
    uint64_t calculate() {
        uint32_t selector = selector_in() % 8;
        uint32_t value = uint32_t(data_in());
        uint64_t result = 0;
        uint32_t i;
        switch (selector) {
        case 0:
            return value;
        case 1:
            result = 10;
            if (value & 1) break;
            result += 20;
            [[fallthrough]];
        case 2:
        case 3:
            result += 40;
            break;
        default:
            result = 50;
            [[fallthrough]];
        case 4:
            result += 60;
            break;
        case 5:
            selector = 0;
            switch (value & 3) {
            case 0: result = 70; break;
            case 1: return 80;
            default: result = 90; break;
            }
            result += selector;
            break;
        }
        for (i = 0; i < 3; ++i) {
            switch (i) {
            case 0: result += 1; break;
            case 1: continue;
            default: result += 3; break;
            }
            result += 5;
        }
        return result;
    }
    void _assign() { result_out = _ASSIGN(calculate()); }
};
extern SwitchControl cpphdl_top;
