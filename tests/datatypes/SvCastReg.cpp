#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

long _system_clock = 0;

int main()
{
    reg<logic<8>> logic_reg;
    logic_reg = logic<8>(0xa5);
    logic<8> logic_value = sv_cast<logic<8>>(logic_reg);
    if ((uint64_t)logic_value != 0xa5) {
        return 1;
    }

    reg<u32> u32_reg;
    u32_reg = u32(0x12345678u);
    u32 u32_value = sv_cast<u32>(u32_reg);
    if ((uint64_t)u32_value != 0x12345678u) {
        return 2;
    }

    reg<logic<8>> cast_reg = sv_cast<reg<logic<8>>>(0x5a);
    if ((uint64_t)static_cast<logic<8>&>(cast_reg) != 0x5a) {
        return 3;
    }

    array<4, logic<4>, true> packed_array = sv_cast<array<4, logic<4>, true>>(3);
    for (size_t i = 0; i < 4; ++i) {
        if ((uint64_t)packed_array[i] != 3) {
            return 4;
        }
    }

    return 0;
}
