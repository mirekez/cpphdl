#include <cpphdl.h>
using namespace cpphdl;

template<unsigned WIDTH>
class NativeHarnessDut : public Module {
public:
    logic<WIDTH> result;
    void _work(bool reset) { result = reset ? 0 : 6; }
};

// A host harness must not inherit Module. The converter must still discover
// its hardware members, without exporting the host's lambda closures.
template<unsigned WIDTH>
class NativeLambdaHarness {
public:
    NativeHarnessDut<WIDTH> dut;
    bool run() {
        uint64_t accumulator = 0;
        auto append = [&](uint64_t value, uint64_t width) {
            accumulator = (accumulator << width) | value;
        };
        append(1, 1);
        append(2, 2);
        dut._work(false);
        return accumulator == 6 && dut.result == accumulator;
    }
};

int main() {
    return NativeLambdaHarness<8>().run() && NativeLambdaHarness<16>().run() ? 0 : 1;
}
