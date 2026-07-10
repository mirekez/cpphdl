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

struct RawBytes8 : public bitops<RawBytes8>
{
    constexpr static size_t SIZE = 8;
    uint8_t bytes[SIZE];

    using bitops<RawBytes8>::operator=;
    using bitops<RawBytes8>::operator&;
};

struct CRawBytes8
{
    uint8_t bytes[8];
    uint32_t guard;
};

struct CpphdlRawBytes8
{
    RawBytes8 value;
    uint32_t guard;
};

struct RawWords2 : public bitops<RawWords2>
{
    constexpr static size_t SIZE = sizeof(uint32_t[2]);
    uint32_t words[2];

    using bitops<RawWords2>::operator=;
};

struct CRawWords2
{
    uint32_t words[2];
    uint32_t guard;
};

struct CpphdlRawWords2
{
    RawWords2 value;
    uint32_t guard;
};

static_assert(std::is_empty_v<bitops<RawBytes8>>);
static_assert(sizeof(RawBytes8) == sizeof(uint8_t[8]));
static_assert(alignof(RawBytes8) == alignof(uint8_t[8]));
static_assert(offsetof(RawBytes8, bytes) == 0);
static_assert(sizeof(CRawBytes8) == sizeof(CpphdlRawBytes8));
static_assert(offsetof(CRawBytes8, guard) == offsetof(CpphdlRawBytes8, guard));
static_assert(sizeof(RawWords2) == sizeof(uint32_t[2]));
static_assert(alignof(RawWords2) == alignof(uint32_t[2]));
static_assert(offsetof(RawWords2, words) == 0);
static_assert(sizeof(CRawWords2) == sizeof(CpphdlRawWords2));
static_assert(offsetof(CRawWords2, guard) == offsetof(CpphdlRawWords2, guard));

int main()
{
    uint8_t raw[8] = {0x01u, 0x23u, 0x45u, 0x67u, 0x89u, 0xabu, 0xcdu, 0xefu};
    CpphdlRawBytes8 hbytes{};
    CRawBytes8 cbytes{};
    hbytes.guard = cbytes.guard = 0x11223344u;
    std::memcpy(cbytes.bytes, raw, sizeof(raw));
    hbytes.value = raw;
    if (hbytes.guard != 0x11223344u || std::memcmp(&hbytes, &cbytes, sizeof(hbytes)) != 0) {
        return 1;
    }

    RawBytes8 mask{};
    uint8_t mask_raw[8] = {0xffu, 0x00u, 0xffu, 0x00u, 0xffu, 0x00u, 0xffu, 0x00u};
    mask = mask_raw;
    hbytes.value = hbytes.value & mask;
    for (size_t i = 0; i < sizeof(raw); ++i) {
        cbytes.bytes[i] = raw[i] & mask_raw[i];
    }
    if (hbytes.guard != 0x11223344u || std::memcmp(&hbytes, &cbytes, sizeof(hbytes)) != 0) {
        return 2;
    }

    uint32_t raw_words[2] = {0x01234567u, 0x89abcdefu};
    CpphdlRawWords2 hwords{};
    CRawWords2 cwords{};
    hwords.guard = cwords.guard = 0xa5a55a5au;
    std::memcpy(cwords.words, raw_words, sizeof(raw_words));
    hwords.value = raw_words;
    if (hwords.guard != 0xa5a55a5au || std::memcmp(&hwords, &cwords, sizeof(hwords)) != 0) {
        return 3;
    }

    return 0;
}
