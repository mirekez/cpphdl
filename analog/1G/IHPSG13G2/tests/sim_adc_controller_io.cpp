#include "sim_adc_controller.cpp"

// Exercise the adapter's real streams through its public co-simulation ABI.
// This checks evidence transport; the ngspice bridge has separate tests.
int main(int argc, char** argv) {
    if (argc != 3 && argc != 4)
        return 2;
    const char* arguments[] = {argc == 4 ? argv[3] : "1", "1", argv[1], argv[2]};
    co_info info{0, 0, 0, nullptr, nullptr, nullptr,
                 [](co_info*, unsigned, Digital_t*) {}, nullptr, 0, After_input,
                 0, 4, nullptr, arguments, nullptr};
    Cosim_setup(&info);
    Digital_t low{ZERO, STRONG}, high{ONE, STRONG};
    info.in_fn(&info, 1, &low);
    info.in_fn(&info, 0, &low);
    info.step(&info);
    for (unsigned clock = 0; clock < 2; ++clock) {
        info.vtime = (clock + .5) * 4e-9;
        info.in_fn(&info, 0, &high);
        info.step(&info);
        info.in_fn(&info, 0, &low);
        info.step(&info);
    }
    info.cleanup(&info);
    return 0;
}
