#include "generated/blocking_array_update.h"
#ifdef CPPHDL_TEST_OPTIMIZED
#include "blocking_array_update_optimized_combs.h"
#endif
#include <cstdio>

long _system_clock = 0;

int main()
{
    blocking_array_update forward;
    blocking_array_update reverse;
    cpphdl::logic<1> valid = 0;
    cpphdl::logic<32> data = 0;
    for (auto* model : {&forward, &reverse}) {
        model->valid_i_in = _ASSIGN_REG(valid);
        model->data_i_in = _ASSIGN_REG(data);
        model->_assign();
#ifdef CPPHDL_TEST_OPTIMIZED
        bind_optimized_ports(*model);
#endif
    }
    for (unsigned step = 0; step < 64; ++step) {
        ++_system_clock;
        valid = step & 1;
        data = 0x87654321 + step;
#ifdef CPPHDL_TEST_OPTIMIZED
        calc_all(forward, false);
        calc_all(reverse, false);
#endif
        const auto forwardValid = uint64_t(forward.front_valid_o_out());
        const auto forwardData = uint64_t(forward.front_data_o_out());
        const auto forwardReady = uint64_t(forward.independent_ready_o_out());
        const auto reverseReady = uint64_t(reverse.independent_ready_o_out());
        const auto reverseData = uint64_t(reverse.front_data_o_out());
        const auto reverseValid = uint64_t(reverse.front_valid_o_out());
        if (forwardValid != uint64_t(valid) || reverseValid != uint64_t(valid) ||
            forwardData != uint64_t(data) || reverseData != uint64_t(data) ||
            forwardReady != !bool(valid) || reverseReady != !bool(valid)) {
            std::fprintf(stderr, "blocking array update mismatch at step %u\n", step);
            return 1;
        }
    }
}
