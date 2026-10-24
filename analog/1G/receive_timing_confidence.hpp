#pragma once
#include <cpphdl.h>
using namespace cpphdl;

// Timing evidence window: mean applied proportional correction <=64 ppm,
// each 128-symbol mean <=512 ppm, period movement <=32 ppm over 1024 updates.
class ReceiveTimingConfidence : public Module {
public:
    _PORT(u<14>) error_in;
    _PORT(u<25>) period_in;
    _PORT(bool) valid_in;
    _PORT(bool) fault_in;
    _PORT(bool) ready_out = _ASSIGN(ready && !fault_in());

private:
    reg<u<10>> count{};
    reg<u<11>> excited{};
    reg<u<23>> magnitude{};
    reg<u<24>> total{};
    reg<u<21>> partial{};
    reg<u<25>> first_period{};
    reg<u1> failed{}, ready{};

public:
    void _assign() {}
    void _work(bool reset) {
        int e = int(unsigned(error_in()));
        int t = int(unsigned(total));
        int p = int(unsigned(partial));
        int slew = int(unsigned(period_in())) - int(unsigned(first_period));
        bool bad = bool(failed);
        unsigned next_excited;
        unsigned next_magnitude;
        if (e >= 8192)
            e -= 16384;
        if (t >= 8388608)
            t -= 16777216;
        if (p >= 1048576)
            p -= 2097152;
        next_excited = unsigned(excited) + unsigned(e != 0);
        next_magnitude = unsigned(magnitude) + unsigned(e < 0 ? -e : e);
        t += e;
        p += e;
        if (valid_in()) {
            excited._next = next_excited;
            magnitude._next = next_magnitude;
            if (count == 0)
                first_period._next = period_in();
            total._next = t;
            partial._next = p;
            count._next = count + 1;
            if ((unsigned(count) & 127) == 127) {
                bad = bad || p > 27487 || p < -27487;
                failed._next = bad;
                partial._next = 0;
            }
            if (count == 1023) {
                ready._next = next_excited >= 128 && next_magnitude <= 2097152 && !bad && t <= 27487 && t >= -27487 &&
                              slew <= 537 && slew >= -537;
                total._next = 0;
                excited._next = 0;
                magnitude._next = 0;
                failed._next = 0;
            }
        }
        if (reset || fault_in()) {
            count._next = 0;
            total._next = 0;
            excited._next = 0;
            magnitude._next = 0;
            partial._next = 0;
            first_period._next = 0;
            failed._next = 0;
            ready._next = 0;
        }
    }
    void _strobe() {
        excited.strobe();
        magnitude.strobe();
        count.strobe();
        total.strobe();
        partial.strobe();
        first_period.strobe();
        failed.strobe();
        ready.strobe();
    }
};
