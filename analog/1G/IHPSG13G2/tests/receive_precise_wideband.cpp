#include "../precise_wideband_receive_frontend.hpp"
template <std::size_t Count>
using ReceiveFrontend = analog::one_g::ihp_sg13g2::precise_wideband_receive_frontend<Count>;
#include "receive_acquisition_fixture.hpp"
