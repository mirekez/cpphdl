#include <cpphdl.h>
using namespace cpphdl;

enum class CastByte : signed char { zero = 0 };
struct CastParent { uint32_t field; };
struct CastChild : CastParent {};
struct CastPacket { uint32_t data; uint16_t tag; };
struct CastPacketView { uint32_t data; uint16_t tag; };

// Return widened results so that the output assignment cannot hide a lost
// narrow cast. The same expressions exercise all three value-cast spellings.
#define CAST_MATRIX(C) \
    case 0: return C(uint8_t, x); \
    case 1: return C(int8_t, x); \
    case 2: return C(uint16_t, x); \
    case 3: return C(int16_t, x); \
    case 4: return C(uint32_t, x); \
    case 5: return C(int32_t, x); \
    case 6: return C(unsigned long long, x); \
    case 7: return C(long long, x); \
    case 8: return C(char, x); \
    case 9: return C(char8_t, x); \
    case 10: return C(char16_t, x); \
    case 11: return C(char32_t, x); \
    case 12: return C(wchar_t, x); \
    case 13: return C(bool, x); \
    case 14: return C(uint64_t, C(int8_t, x)); \
    case 15: return C(uint32_t, C(int8_t, x)); \
    case 16: return C(int64_t, C(uint32_t, x)); \
    case 17: return C(int64_t, C(int32_t, x)); \
    case 18: return C(uint8_t, x) + 257u; \
    case 19: return C(int8_t, x) + 257; \
    case 20: return C(int8_t, x) < 0; \
    case 21: return C(int8_t, x) < 1u; \
    case 22: return C(int32_t, x) >> 7; \
    case 23: return C(uint32_t, x) >> 7; \
    case 24: return C(uint64_t, C(uint8_t, x)) << 40; \
    case 25: return C(uint64_t, C(int8_t, x)) >> 40; \
    case 26: return C(int64_t, C(CastByte, x)); \
    case 27: return C(const unsigned short, x); \
    case 28: return C(uint64_t, C(unsigned __int128, x)); \
    case 29: return C(uint64_t, C(__int128, C(int8_t, x))); \
    case 30: return C(uint64_t, C(logic<8>, x)); \
    case 31: return C(uint64_t, C(u<7>, x)); \
    case 32: return C(int64_t, C(logic<7>, x)); \
    case 33: return C(uint64_t, C(logic<13>, x)) + 8192u; \
    case 34: return C(bool, C(uint8_t, x)); \
    case 35: return C(uint16_t, C(int8_t, x)); \
    case 36: return C(unsigned long long, C(uint8_t, x)) << 48; \
    case 37: return C(bool, C(int8_t, x)) ? C(int8_t, x) : 257; \
    case 38: return C(uint8_t, x + 1); \
    case 39: return C(int8_t, x) / 3; \
    case 40: return references(x); \
    case 41: return enum_local(x); \
    case 42: return hierarchy(x); \
    case 43: return C(uint32_t, x) + 1u; \
    case 44: return C(uint32_t, x) * 65537u; \
    case 45: return ~C(uint32_t, x); \
    case 46: return C(logic<64>, C(uint32_t, x) + 1u); \
    case 47: return character_locals(x); \
    case 48: return struct_references(x); \
    case 49: return signed_reference(x); \
    default: return references(x);

#define STATIC_CAST(T, x) static_cast<T>(x)
#define CSTYLE_CAST(T, x) ((T)(x))
// An alias permits functional casts to multi-token primitive types too.
template<class T> using CastIdentity = T;
#define FUNCTIONAL_CAST(T, x) CastIdentity<T>(x)

class CastMatrix : public Module {
public:
    _PORT(logic<64>) value_in;
    _PORT(logic<8>) mode_in;
    _PORT(logic<64>) static_out;
    _PORT(logic<64>) cstyle_out;
    _PORT(logic<64>) functional_out;
    logic<64> static_result_comb, cstyle_result_comb, functional_result_comb;

    logic<64>& static_result_comb_func() {
        static_result_comb = as_static(value_in(), mode_in());
        return static_result_comb;
    }
    logic<64>& cstyle_result_comb_func() {
        cstyle_result_comb = as_cstyle(value_in(), mode_in());
        return cstyle_result_comb;
    }
    logic<64>& functional_result_comb_func() {
        functional_result_comb = as_functional(value_in(), mode_in());
        return functional_result_comb;
    }

    uint64_t references(uint64_t x) {
        uint32_t value;
        value = static_cast<uint32_t>(x);
        static_cast<uint32_t&>(value) = value ^ 0x10203040u;
        const_cast<uint32_t&>(static_cast<const uint32_t&>(value)) = value + 1u;
        reinterpret_cast<uint32_t&>(value) = value ^ 0x01010101u;
        // Casting to void must still evaluate the operand exactly once.
        static_cast<void>(value += 7u);
        (void)(value += 11u);
        (void)value;
        static_cast<void>(x + 1u);
        *static_cast<uint32_t*>(static_cast<void*>(&value)) ^= 1u;
        *const_cast<uint32_t*>(static_cast<const uint32_t*>(&value)) += 2u;
        *reinterpret_cast<uint32_t*>(&value) += 3u;
        return value;
    }
    uint64_t enum_local(uint64_t x) {
        CastByte value;
        value = static_cast<CastByte>(x);
        return static_cast<int64_t>(value);
    }
    uint64_t hierarchy(uint64_t x) {
        CastChild value;
        value.field = static_cast<uint32_t>(x);
        dynamic_cast<CastParent&>(value).field = value.field ^ 0x11223344u;
        return static_cast<const CastParent&>(value).field;
    }
    uint64_t character_locals(uint64_t x) {
        char8_t a;
        char16_t b;
        char32_t c;
        wchar_t d;
        unsigned long long e;
        a = static_cast<char8_t>(x);
        b = static_cast<char16_t>(x);
        c = static_cast<char32_t>(x);
        d = static_cast<wchar_t>(x);
        e = static_cast<unsigned long long>(x);
        return static_cast<uint64_t>(a) ^ (static_cast<uint64_t>(b) << 8)
            ^ (static_cast<uint64_t>(c) << 24) ^ static_cast<uint64_t>(d) ^ e;
    }
    uint64_t struct_references(uint64_t x) {
        CastPacket packet;
        packet.data = uint32_t(x);
        packet.tag = uint16_t(x >> 32);
        // Round trips through a different reference type are valid C++: only
        // access the object after casting back to its actual type. In RTL all
        // these reference views must retain the original assignable signal.
        ((CastPacket&)(CastPacketView&)packet).data ^= 0x12345678u;
        reinterpret_cast<CastPacket&>(reinterpret_cast<CastPacketView&>(packet)).tag += 3;
        const_cast<CastPacket&>(static_cast<const CastPacket&>(packet)).data += 7u;
        return uint64_t(((const CastPacket&)(const CastPacketView&)packet).data)
            | (uint64_t(packet.tag) << 32);
    }
    uint64_t signed_reference(uint64_t x) {
        uint32_t value;
        value = uint32_t(x);
        // Access through the corresponding signed type is permitted in C++.
        reinterpret_cast<int32_t&>(value) = 123;
        return value;
    }
    uint64_t as_static(uint64_t x, unsigned selector) {
        switch (selector) { CAST_MATRIX(STATIC_CAST) }
    }
    uint64_t as_cstyle(uint64_t x, unsigned selector) {
        switch (selector) { CAST_MATRIX(CSTYLE_CAST) }
    }
    uint64_t as_functional(uint64_t x, unsigned selector) {
        switch (selector) { CAST_MATRIX(FUNCTIONAL_CAST) }
    }
    void _assign() {
        static_out = _ASSIGN_COMB(static_result_comb_func());
        cstyle_out = _ASSIGN_COMB(cstyle_result_comb_func());
        functional_out = _ASSIGN_COMB(functional_result_comb_func());
    }
    void _work(bool reset) {}
    void _strobe() {}
};
#undef STATIC_CAST
#undef CSTYLE_CAST
#undef FUNCTIONAL_CAST
#undef CAST_MATRIX
