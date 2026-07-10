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

struct CArray2D
{
    uint16_t value[2][3];
    uint32_t tail[2];
    uint64_t guard;
};

struct CpphdlArray2D
{
    array2D<2, 3, uint16_t> value;
    uint32_t tail[2];
    uint64_t guard;
};

struct CArrayLeaf
{
    uint16_t value[2][3];
    uint8_t tail[5];
    uint32_t guard;
};

struct CpphdlArrayLeaf
{
    array<2, uint16_t[3]> value;
    uint8_t tail[5];
    uint32_t guard;
};

struct CArray4D
{
    uint8_t value[2][3][2][2];
    uint16_t tail;
    uint32_t guard;
};

struct CpphdlArray4D
{
    array4D<2, 3, 2, 2, uint8_t> value;
    uint16_t tail;
    uint32_t guard;
};

struct CArrayOps
{
    uint16_t value[3];
    uint32_t guard;
};

struct CpphdlArrayOps
{
    array<3, uint16_t> value;
    uint32_t guard;
};

static_assert(sizeof(array<3, uint16_t>) == sizeof(uint16_t[3]));
static_assert(alignof(array<3, uint16_t>) == alignof(uint16_t[3]));
static_assert(sizeof(array2D<2, 3, uint16_t>) == sizeof(uint16_t[2][3]));
static_assert(alignof(array2D<2, 3, uint16_t>) == alignof(uint16_t[2][3]));
static_assert(sizeof(array<2, uint16_t[3]>) == sizeof(uint16_t[2][3]));
static_assert(alignof(array<2, uint16_t[3]>) == alignof(uint16_t[2][3]));
static_assert(sizeof(array4D<2, 3, 2, 2, uint8_t>) == sizeof(uint8_t[2][3][2][2]));
static_assert(alignof(array4D<2, 3, 2, 2, uint8_t>) == alignof(uint8_t[2][3][2][2]));
static_assert(sizeof(CArray2D) == sizeof(CpphdlArray2D));
static_assert(offsetof(CArray2D, tail) == offsetof(CpphdlArray2D, tail));
static_assert(offsetof(CArray2D, guard) == offsetof(CpphdlArray2D, guard));
static_assert(sizeof(CArrayLeaf) == sizeof(CpphdlArrayLeaf));
static_assert(offsetof(CArrayLeaf, tail) == offsetof(CpphdlArrayLeaf, tail));
static_assert(offsetof(CArrayLeaf, guard) == offsetof(CpphdlArrayLeaf, guard));
static_assert(sizeof(CArray4D) == sizeof(CpphdlArray4D));
static_assert(offsetof(CArray4D, tail) == offsetof(CpphdlArray4D, tail));
static_assert(offsetof(CArray4D, guard) == offsetof(CpphdlArray4D, guard));

int main()
{
    CArray2D c2{};
    CpphdlArray2D h2{};
    for (size_t y = 0; y < 2; ++y) {
        for (size_t x = 0; x < 3; ++x) {
            c2.value[y][x] = static_cast<uint16_t>(0x1100u + y * 0x10u + x);
            h2.value[y][x] = c2.value[y][x];
        }
    }
    c2.tail[0] = h2.tail[0] = 0x55667788u;
    c2.tail[1] = h2.tail[1] = 0x99aabbccu;
    c2.guard = h2.guard = 0x1122334455667788ull;
    if (!same_bytes(c2, h2)) {
        return 1;
    }

    CArrayLeaf cleaf{};
    CpphdlArrayLeaf hleaf{};
    for (size_t y = 0; y < 2; ++y) {
        for (size_t x = 0; x < 3; ++x) {
            cleaf.value[y][x] = static_cast<uint16_t>(0x2200u + y * 0x10u + x);
            hleaf.value[y][x] = cleaf.value[y][x];
        }
    }
    for (size_t i = 0; i < 5; ++i) {
        cleaf.tail[i] = hleaf.tail[i] = static_cast<uint8_t>(0xa0u + i);
    }
    cleaf.guard = hleaf.guard = 0xdeadbeefu;
    if (!same_bytes(cleaf, hleaf)) {
        return 2;
    }

    CArray4D c4{};
    CpphdlArray4D h4{};
    for (size_t a = 0; a < 2; ++a) {
        for (size_t b = 0; b < 3; ++b) {
            for (size_t c = 0; c < 2; ++c) {
                for (size_t d = 0; d < 2; ++d) {
                    c4.value[a][b][c][d] = static_cast<uint8_t>(1 + a * 12 + b * 4 + c * 2 + d);
                    h4.value[a][b][c][d] = c4.value[a][b][c][d];
                }
            }
        }
    }
    c4.tail = h4.tail = 0xa55au;
    c4.guard = h4.guard = 0x12345678u;
    if (!same_bytes(c4, h4)) {
        return 3;
    }

    CpphdlArrayOps ops{};
    uint16_t raw[3] = {0x0102u, 0x0304u, 0x0506u};
    array<3, uint16_t> constructed(raw);
    if (std::memcmp(&constructed, raw, sizeof(raw)) != 0) {
        return 4;
    }

    ops.guard = 0xcafebabeu;
    ops.value = raw;
    if (ops.guard != 0xcafebabeu || std::memcmp(&ops.value, raw, sizeof(raw)) != 0) {
        return 5;
    }
    ops.value.bits(15, 8) = logic<8>(0x77);
    uint16_t raw_after_bits[3] = {0x7702u, 0x0304u, 0x0506u};
    if (ops.guard != 0xcafebabeu || std::memcmp(&ops.value, raw_after_bits, sizeof(raw_after_bits)) != 0) {
        return 6;
    }

    CpphdlArrayOps masked{};
    array<3, uint16_t> mask;
    uint16_t mask_raw[3] = {0x00ffu, 0xffffu, 0x0f0fu};
    uint16_t raw_after_mask[3] = {0x0002u, 0x0304u, 0x0506u & 0x0f0fu};
    masked.guard = 0x89abcdefu;
    masked.value = raw;
    mask = mask_raw;
    masked.value = masked.value & mask;
    if (masked.guard != 0x89abcdefu || std::memcmp(&masked.value, raw_after_mask, sizeof(raw_after_mask)) != 0) {
        return 7;
    }

    return 0;
}
