#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

using namespace cpphdl;

long _system_clock = 0;

template<typename T, typename U>
static bool same_bytes(const T& lhs, const U& rhs)
{
    static_assert(sizeof(T) == sizeof(U));
    return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

struct CRawLogic9
{
    uint8_t value[2];
    uint8_t tail[3];
    uint32_t guard;
};

struct CpphdlLogic9
{
    logic<9> value;
    uint8_t tail[3];
    uint32_t guard;
};

struct CRawLogic64
{
    uint8_t value[8];
    uint32_t guard;
};

struct CpphdlLogic64
{
    logic<64> value;
    uint32_t guard;
};

static_assert(std::is_empty_v<bitops<logic<8>>>);
static_assert(sizeof(logic<1>) == sizeof(uint8_t[1]));
static_assert(sizeof(logic<8>) == sizeof(uint8_t[1]));
static_assert(sizeof(logic<9>) == sizeof(uint8_t[2]));
static_assert(sizeof(logic<16>) == sizeof(uint8_t[2]));
static_assert(sizeof(logic<31>) == sizeof(uint8_t[4]));
static_assert(sizeof(logic<64>) == sizeof(uint8_t[8]));
static_assert(alignof(logic<64>) == alignof(uint8_t[8]));
static_assert(sizeof(CRawLogic9) == sizeof(CpphdlLogic9));
static_assert(offsetof(CRawLogic9, tail) == offsetof(CpphdlLogic9, tail));
static_assert(offsetof(CRawLogic9, guard) == offsetof(CpphdlLogic9, guard));
static_assert(sizeof(CRawLogic64) == sizeof(CpphdlLogic64));
static_assert(offsetof(CRawLogic64, guard) == offsetof(CpphdlLogic64, guard));

int main()
{
    logic<9> all_ones = 0xffffu;
    uint8_t raw_ones[2] = {0xffu, 0x01u};
    if (std::memcmp(&all_ones, raw_ones, sizeof(raw_ones)) != 0) {
        return 1;
    }

    CpphdlLogic9 h9{};
    CRawLogic9 c9{};
    h9.value = 0x155u;
    c9.value[0] = 0x55u;
    c9.value[1] = 0x01u;
    for (size_t i = 0; i < 3; ++i) {
        h9.tail[i] = c9.tail[i] = static_cast<uint8_t>(0xa0u + i);
    }
    h9.guard = c9.guard = 0x12345678u;
    if (!same_bytes(h9, c9)) {
        return 2;
    }

    h9.value |= logic<9>(0x0aau);
    c9.value[0] = 0xffu;
    c9.value[1] = 0x01u;
    if (h9.guard != 0x12345678u || !same_bytes(h9, c9)) {
        return 3;
    }

    CpphdlLogic64 h64{};
    CRawLogic64 c64{};
    h64.value = logic<64>(0x1122334455667788ull);
    uint8_t raw64[8] = {0x88u, 0x77u, 0x66u, 0x55u, 0x44u, 0x33u, 0x22u, 0x11u};
    std::memcpy(c64.value, raw64, sizeof(raw64));
    h64.guard = c64.guard = 0xcafebabeu;
    if (!same_bytes(h64, c64)) {
        return 4;
    }

    return 0;
}
