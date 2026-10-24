#include "../IHPSG13G2/cascoded_core_transmit_driver.hpp"
#include "core_transmit_fixture.hpp"

int main(int argc, char** argv) {
    return run_core_transmit_driver<analog::one_g::ihp_sg13g2::cascoded_core_emphasized_pam5_driver, true>(argc, argv);
}
