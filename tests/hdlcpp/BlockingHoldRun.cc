#include <cassert>
#include <cstdio>
#if defined(HOLD_GRAPH)
#include "model.h"
#elif defined(HOLD_RTL)
#include "VBlockingHold.h"
#else
#include "generated/BlockingHold.h"
long _system_clock = 0;
#endif

int main() {
#if defined(HOLD_GRAPH)
    cpphdl_native::Model model;
#elif defined(HOLD_RTL)
    VBlockingHold model;
#else
    static BlockingHold<HOLD_ENABLED> model;
    cpphdl::logic<1> capture;
    cpphdl::logic<8> data;
    model.capture_in = _ASSIGN(capture);
    model.data_in = _ASSIGN(data);
    model._assign();
#endif
    unsigned held = 0, observed = 0, direct = 0, count = 0;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        const bool reset = sample % 127 == 0;
        const bool captureValue = sample % 3 == 1;
        const unsigned dataValue = (sample * 73 + 19) & 255;
        if (reset) count = 0;
        else {
            count = (count + 1) & 255;
            if (HOLD_ENABLED && captureValue) {
                held = dataValue;
                observed = held ^ 0x5a;
                direct = held ^ 0xa5;
            }
        }
#if defined(HOLD_GRAPH)
        model.work_reset[0] = reset;
        model.capture[0] = captureValue;
        model.data[0] = dataValue;
        model.step();
        assert(model.held[0] == held);
        assert(model.after_write[0] == observed);
        assert(model.direct[0] == direct);
        assert(model.count[0] == count);
#elif defined(HOLD_RTL)
        model.reset = reset;
        model.capture = captureValue;
        model.data = dataValue;
        model.clock = 0;
        model.eval();
        model.clock = 1;
        model.eval();
        assert(model.held == held);
        assert(model.after_write == observed);
        assert(model.direct == direct);
        assert(model.count == count);
#else
        capture = captureValue;
        data = dataValue;
        ++_system_clock;
        model._work(reset);
        model._strobe();
        assert(uint64_t(model.held_out()) == held);
        assert(uint64_t(model.after_write_out()) == observed);
        assert(uint64_t(model.direct_out()) == direct);
        assert(uint64_t(model.count_out()) == count);
#endif
    }
    std::puts("clocked blocking retention: 4096 oracle samples PASS");
}
