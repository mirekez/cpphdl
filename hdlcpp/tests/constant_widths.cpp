#include "generated/constant_widths.h"
#include <cstdint>
#include <cstdio>
#include <type_traits>

long _system_clock = 0;

template<unsigned Width, unsigned IdWidth, uint64_t Large, unsigned Narrow>
bool check()
{
    using Model = constant_widths<Width, IdWidth, Large, Narrow>;
    static_assert(std::is_same_v<typename Model::strb_t, cpphdl::logic<Width / 8>>);
    static_assert(std::is_same_v<typename Model::direct_t, typename Model::strb_t>);
    static_assert(std::is_same_v<typename Model::id_t, cpphdl::logic<IdWidth>>);
    static_assert(std::is_same_v<typename Model::truncated_t, cpphdl::logic<255>>);
    static_assert(std::is_same_v<typename Model::clipped_t, cpphdl::logic<8>>);
    Model model{};
    model._assign();
    return uint64_t(model.strobe_width_o_out()) == Width / 8 &&
           uint64_t(model.large_half_o_out()) == Large / 2 &&
           uint64_t(model.narrow_half_o_out()) == (Narrow & 255) / 2 &&
           uint64_t(model.truncated_half_o_out()) == 127 &&
           uint64_t(model.unsigned_sum_o_out()) == uint64_t(IdWidth) + 1 &&
           uint64_t(model.extended_o_out()) == 0x100000000ull;
}

int main()
{
    if (!check<64, 4, 0x100000000ull, 255>() ||
        !check<128, 8, 0xffffffffffffffffull, 128>() ||
        !check<32, 1, 0x8000000000000000ull, 511>()) {
        std::fputs("constant width value mismatch\n", stderr);
        return 1;
    }
    std::puts("constant widths: aliases and boundary values pass");
}
