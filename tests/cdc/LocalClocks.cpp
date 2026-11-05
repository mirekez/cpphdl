#include <cpphdl.h>
using namespace cpphdl;

class LocalClockPair : public Module {
public:
    _PORT(u<16>) source_out, destination_out;
private:
    reg<u<16>> source{}, destination{};
public:
    void _assign() {
        source_out = _ASSIGN_REG(source);
        destination_out = _ASSIGN_REG(destination);
    }
    void _work_source_clk(bool reset) { source._next = reset ? 0 : source + 1; }
    void _strobe_source_clk() { source.strobe(); }
    void _work_destination_clk(bool reset) { destination._next = reset ? 0 : destination + 1; }
#ifndef LOCAL_CLOCK_MISSING_STROBE
    void _strobe_destination_clk() { destination.strobe(); }
#endif
};

class LocalClocks : public Module {
public:
    _PORT(u<16>) a_source_out, a_destination_out, b_source_out, b_destination_out;
private:
    LocalClockPair a, b;
public:
    void _assign() {
        a_source_out = _ASSIGN(a.source_out()); a_destination_out = _ASSIGN(a.destination_out());
        b_source_out = _ASSIGN(b.source_out()); b_destination_out = _ASSIGN(b.destination_out());
        a._assign(); b._assign();
    }
    void _work_fast_clk(bool reset) {
        a._work_source_clk(reset); b._work_destination_clk(reset);
    }
    void _work_slow_clk(bool reset) {
        a._work_destination_clk(reset); b._work_source_clk(reset);
    }
    void _strobe_fast_clk() {
        a._strobe_source_clk();
#ifndef LOCAL_CLOCK_MISSING_STROBE
        b._strobe_destination_clk();
#endif
    }
    void _strobe_slow_clk() {
#ifndef LOCAL_CLOCK_MISSING_STROBE
        a._strobe_destination_clk();
#endif
        b._strobe_source_clk();
    }
    void _work_spare_clk(bool) {}
    void _strobe_spare_clk() {}
};
