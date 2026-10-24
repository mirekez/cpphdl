#include "../charge_balanced_receive_frontend.hpp"
#include "programmable_conversion_fixture.hpp"

int main(int argc, char** argv) {
    return run_programmable_conversion<analog::one_g::ihp_sg13g2::charge_balanced_receive_frontend>(argc, argv);
}
