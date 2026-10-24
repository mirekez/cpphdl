#include "../parallel_receive_frontend.hpp"
template <std::size_t Count>
using ConversionFrontend = analog::one_g::ihp_sg13g2::parallel_receive_frontend<Count>;
#include "receive_conversion_fixture.hpp"
