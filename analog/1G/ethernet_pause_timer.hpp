#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// 125 MHz GMII: one pause quantum is 64 clocks (512 bit times).
// Finish any active frame first. Control frames bypass this data-only hold.
class EthernetPauseTimer : public Module {
public:
    _PORT(bool) command_in;
    _PORT(u<16>) quanta_in;
    _PORT(bool) data_active_in;
    _PORT(bool) enable_in;
    _PORT(bool) hold_out = _ASSIGN(enable_in() && remaining != 0);
private:
    reg<u<22>> remaining{};
public:
    void _assign() {}
    void _work(bool reset) {
        if (!data_active_in() && remaining != 0)
            remaining._next = remaining - 1;
        if (command_in())
            remaining._next = unsigned(quanta_in()) << 6;
        if (reset || !enable_in()) remaining._next = 0;
    }
    void _strobe() { remaining.strobe(); }
};
