#include "cpphdl.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace cpphdl;

struct FloatWord {
    uint32_t raw;
    float unpack() const { float result; std::memcpy(&result, &raw, sizeof(result)); return result; }
    FloatWord pack(float input) { std::memcpy(&raw, &input, sizeof(raw)); return *this; }
    FloatWord operator*(FloatWord other) { return FloatWord{}.pack(unpack()*other.unpack()); }
    FloatWord operator+(FloatWord other) { return FloatWord{}.pack(unpack()+other.unpack()); }
};
class NativeFloat : public Module {
public:
    _PORT(uint32_t) a_in, b_in;
    _PORT(array<11,uint32_t>) result_out = _ASSIGN_REG(result);
    reg<array<11,uint32_t>> result;
    void _work(bool reset) {
        FloatWord a{a_in()}, b{b_in()}, output;
        output = a*b;
        result._next[0] = output.raw;
        output = a+b;
        result._next[1] = output.raw;
        output.pack(a.unpack()/b.unpack());
        result._next[2] = output.raw;
        output.pack(::sqrt(double(a.unpack())));
        result._next[3] = output.raw;
        output.pack(::tanh(double(a.unpack())));
        result._next[4] = output.raw;
        output.pack(-a.unpack());
        result._next[5] = output.raw;
        result._next[6] = a.unpack() <= b.unpack();
        result._next[7] = bool(a.unpack());
        output.pack(std::exp(a.unpack()));
        result._next[8] = output.raw;
        output.pack(std::sqrt(a.unpack()));
        result._next[9] = output.raw;
        output.pack(std::tanh(a.unpack()));
        result._next[10] = output.raw;
        if (reset) result._next = array<11,uint32_t>{};
    }
    void _strobe() { result.strobe(); }
};
extern NativeFloat cpphdl_top;
#ifdef CHECK_NATIVE_FLOAT
#include "model.h"
long _system_clock = 0;
int main() {
    NativeFloat dut{};
    cpphdl_native::Model model;
    uint32_t a = 0, b = 0;
    dut.a_in = _ASSIGN(a);
    dut.b_in = _ASSIGN(b);
    const uint32_t special[] = {0, 0x80000000u, 1, 0x007fffffu, 0x00800000u,
        0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u, 0x7fc00000u};
    for (unsigned i = 0; i < 4096; ++i) {
        a = i < 100 ? special[i/10] : std::bit_cast<uint32_t>(float(int(i%129)-64)/19);
        b = i < 100 ? special[i%10] : std::bit_cast<uint32_t>(float(int(i%71)-35)/7);
        bool reset = i%43 == 0;
        ++_system_clock;
        dut._work(reset); dut._strobe();
        model.a[0] = a; model.b[0] = b; model.work_reset[0] = reset;
        model.step();
        for (unsigned lane = 0; lane < 11; ++lane) {
            uint32_t expected = dut.result_out()[lane], got = model.result[lane];
            if (expected != got && !((lane < 6 || lane >= 8) && std::isnan(std::bit_cast<float>(expected)) &&
                std::isnan(std::bit_cast<float>(got)))) {
                std::printf("float mismatch sample=%u lane=%u a=%08x b=%08x got=%08x expected=%08x\n", i, lane, a, b, got, expected);
                return 1;
            }
        }
    }
    std::puts("native float operators, copies, casts, unary math and IEEE specials: 4096 samples PASS");
}
#endif
