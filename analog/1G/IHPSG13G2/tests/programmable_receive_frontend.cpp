#include "../programmable_receive_frontend.hpp"
#include "programmable_receiver_fixture.hpp"

int main(int argc, char** argv) {
    return run_programmable_receiver<analog::one_g::ihp_sg13g2::programmable_receive_frontend<1>>(argc, argv);
}
