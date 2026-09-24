#define NO_MAINFILE
#include "../../examples/basic/Buffer.cpp"
#include <cstdio>
#include <deque>
#ifdef BUFFER_RTL
#include "VBuffer.h"
#endif
#ifndef BUFFER_DEPTH
#define BUFFER_DEPTH 3
#endif

long _system_clock = 0;

struct BufferDriver {
    Buffer<32, BUFFER_DEPTH> model;
    bool valid = false, ready = false;
    logic<32> data = 0;
    void _assign() {
        model.valid_in = _ASSIGN(valid);
        model.ready_in = _ASSIGN(ready);
        model.data_in = _ASSIGN(data);
        model._assign();
    }
};

int main() {
    BufferDriver h;
    std::deque<uint32_t> queue;
    uint32_t random = 0x87654321u;
#ifdef BUFFER_RTL
    VBuffer rtl;
#endif
    h._assign();
    for (unsigned cycle = 0; cycle < 6000; ++cycle) {
        bool reset = cycle < 3 || cycle % 997 == 0;
        bool valid, ready;
        uint32_t data;
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        h.valid = (cycle % 128 < 32) || (random & 1);
        h.ready = (cycle % 128 >= 64) && (random & 2);
        h.data = random;
        ++_system_clock;
        valid = h.model.valid_out();
        ready = h.model.ready_out();
        data = h.model.data_out();
#ifdef BUFFER_RTL
        rtl.clk = 0; rtl.reset = reset;
        rtl.valid_in = h.valid; rtl.ready_in = h.ready; rtl.data_in = random;
        rtl.eval();
        if (!reset && (bool(rtl.valid_out) != valid || bool(rtl.ready_out) != ready
                || (valid && rtl.data_out != data))) {
            std::fprintf(stderr, "Buffer C++/RTL mismatch depth=%u cycle=%u\n", BUFFER_DEPTH, cycle);
            return 1;
        }
#endif
        if (reset) {
            queue.clear();
        } else {
            bool stored = !queue.empty();
            if (valid != (stored || h.valid) || ready != (queue.size() < BUFFER_DEPTH || h.ready)
                || (valid && data != (stored ? queue.front() : random))) {
                std::fprintf(stderr, "Buffer scoreboard mismatch depth=%u cycle=%u\n", BUFFER_DEPTH, cycle);
                return 2;
            }
            if (valid && h.ready && stored) queue.pop_front();
            if (h.valid && ready && (stored || !(valid && h.ready))) queue.push_back(random);
        }
        h.model._work(reset);
#ifdef BUFFER_RTL
        rtl.clk = 1; rtl.eval();
#endif
        h.model._strobe();
    }
    std::printf("Buffer depth=%u: 6000 cycles passed\n", BUFFER_DEPTH);
}
