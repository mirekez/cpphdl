#include "cpphdl.h"
#include <cstdio>
#include <cstdlib>
using namespace cpphdl;
class NativeAssertions : public Module {
public:
    _PORT(uint32_t) data_in;
    _PORT(uint32_t) result_out = _ASSIGN(state[0]);
    _PORT(uint32_t) narrow_out = _ASSIGN(narrow);
    _PORT(uint8_t) flag_out = _ASSIGN(flag);
    reg<u32> state[16];
    reg<u<12>> narrow;
    reg<u1> flag;
    void _work(bool reset) {
        if (data_in() == 123456u) {
            printf("%s: invalid input\n", __inst_name.c_str());
            exit(1);
        }
        for (unsigned i = 0; i < 16; ++i) state[i]._next = reset ? 0 : state[i] + data_in() + i;
        narrow._next = data_in();
        narrow._next |= u<12>(17);
        flag._next = 0;
        flag._next |= 1;
    }
    void _strobe() {
        for (unsigned i = 0; i < 16; ++i) state[i].strobe();
        narrow.strobe(); flag.strobe();
    }
};
extern NativeAssertions cpphdl_top;
#ifdef CHECK_NATIVE_ASSERTIONS
#include "model.h"
#include <stdexcept>
#include <string>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    model.data[0] = 7;
    model.work_reset[0] = 0;
    model.step();
    if (model.result[0] != 7 || model.narrow[0] != (7|17) || model.flag[0] != 1) return 1;
    model.data[0] = 123456;
    model.eval(false);
    try { model.eval(true); return 2; }
    catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("invalid input") == std::string::npos) return 3;
    }
    model.data[0] = 11;
    model.step();
    if (model.result[0] != 18 || model.narrow[0] != (11|17) || model.flag[0] != 1) return 4;
    std::puts("native assertion: failure detected before state commit; eval-only preserved PASS");
}
#endif
