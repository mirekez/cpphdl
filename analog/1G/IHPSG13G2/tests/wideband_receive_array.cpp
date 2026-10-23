#include "../wideband_programmable_receive_frontend.hpp"
#include "programmable_array_fixture.hpp"

int main(int argc, char** argv) {
    return run_programmable_array<analog::one_g::ihp_sg13g2::wideband_programmable_receive_frontend<16>>(argc, argv);
}
