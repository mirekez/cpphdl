#pragma once
#include <cpphdl.h>
#include <iostream>

// Test controller: its registered state advances only through Module lifecycle
// calls. Both electrical simulators must supply their own achieved clock edges.
class ClockEdgeCounter : public cpphdl::Module {
    cpphdl::reg<cpphdl::logic<32>> count{};
public:
    _PORT(cpphdl::logic<32>) Q = _ASSIGN_REG(count);
    void _assign() {}
    void _work(bool reset) { count._next = reset ? 0 : static_cast<std::uint64_t>(count) + 1; }
    void _strobe() { count.strobe(); }
};

inline int run_clock_counter_server() {
    ClockEdgeCounter counter;
    counter._assign();
    counter._work(true);
    counter._strobe();
    int edge;
    std::cout << "0\n" << std::flush;
    while (std::cin >> edge) {
        if (edge != 1)
            return 2;
        ++_system_clock;
        counter._work(false);
        counter._strobe();
        std::cout << static_cast<std::uint64_t>(counter.Q()) << '\n' << std::flush;
    }
    return 0;
}
