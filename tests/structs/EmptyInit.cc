#include <cpphdl.h>
#include <cstdint>
using namespace cpphdl;

struct EmptyInitPlain {
    uint8_t low;
    uint16_t high;
};

struct EmptyInitBits {
    logic<3> low;
    logic<17> high;
};

struct EmptyInitDefaults {
    uint8_t low = 0x35;
    uint16_t high = 0x8642;
};

struct EmptyInitNested {
    EmptyInitPlain inner;
    uint8_t tag;
};

struct EmptyInitNestedDefaults {
    EmptyInitDefaults inner;
    uint8_t tag = 0xa7;
};

struct EmptyInitInstruction {
    union {
        uint32_t raw;
        struct { uint16_t low; uint16_t high; } parts;
    };
};
struct EmptyInitMiddle : EmptyInitInstruction {};
struct EmptyInitDerived : EmptyInitMiddle {};
struct EmptyInitExtended : EmptyInitPlain { uint16_t tag; };
union EmptyInitUnion { uint32_t raw; uint32_t alternative; };

class EmptyInit : public Module {
public:
    _PORT(logic<16>) seed_in;
    _PORT(logic<5>) mode_in;
    _PORT(logic<64>) result_out = _ASSIGN_COMB(result_comb_func());

    logic<64> result_comb;
    logic<64>& result_comb_func() {
        result_comb = calculate(seed_in(), mode_in());
        return result_comb;
    }

    EmptyInitPlain empty_value() { return {}; }

    uint64_t calculate(uint16_t seed, unsigned mode) {
        EmptyInitPlain plain{uint8_t(seed), seed};
        EmptyInitBits bits;
        EmptyInitDefaults defaults{uint8_t(seed), seed};
        EmptyInitNested nested{{uint8_t(seed), seed}, uint8_t(seed)};
        EmptyInitNestedDefaults nested_defaults{{uint8_t(seed), seed}, uint8_t(seed)};
        EmptyInitPlain local{};
        EmptyInitDefaults local_defaults{};
        EmptyInitDerived instruction{{{{uint32_t(seed) | 0xa5a50000u}}}};
        EmptyInitExtended extended{{uint8_t(seed), seed}, 0x5a5a};
        EmptyInitUnion named{uint32_t(seed) | 0x12340000u};

        bits.low = seed;
        bits.high = 0x10000u | seed;
        switch (mode) {
        case 0:
            plain = {};
            break;
        case 1:
            bits = {};
            return uint64_t(bits.low) | (uint64_t(bits.high) << 8);
        case 2:
            plain = EmptyInitPlain{};
            break;
        case 3:
            plain = local;
            break;
        case 4:
            nested = {};
            return uint64_t(nested.inner.low) | (uint64_t(nested.inner.high) << 8)
                | (uint64_t(nested.tag) << 24);
        case 5:
            defaults = {};
            return uint64_t(defaults.low) | (uint64_t(defaults.high) << 8);
        case 6:
            defaults = local_defaults;
            return uint64_t(defaults.low) | (uint64_t(defaults.high) << 8);
        case 7:
            plain = {uint8_t(seed)};
            break;
        case 8:
            nested = {{uint8_t(seed), seed}, 0x5a};
            return uint64_t(nested.inner.low) | (uint64_t(nested.inner.high) << 8)
                | (uint64_t(nested.tag) << 24);
        case 9:
            nested_defaults = {};
            return uint64_t(nested_defaults.inner.low) | (uint64_t(nested_defaults.inner.high) << 8)
                | (uint64_t(nested_defaults.tag) << 24);
        case 10:
            defaults = {uint8_t(seed)};
            return uint64_t(defaults.low) | (uint64_t(defaults.high) << 8);
        case 11:
            plain = empty_value();
            break;
        case 12:
            nested = {{}, 0x5a};
            return uint64_t(nested.inner.low) | (uint64_t(nested.inner.high) << 8)
                | (uint64_t(nested.tag) << 24);
        case 13:
            nested = {{uint8_t(seed)}};
            return uint64_t(nested.inner.low) | (uint64_t(nested.inner.high) << 8)
                | (uint64_t(nested.tag) << 24);
        case 14:
            defaults = EmptyInitDefaults{};
            return uint64_t(defaults.low) | (uint64_t(defaults.high) << 8);
        case 15:
            // GNU C++ compound literals must not add an aggregate nesting level.
            nested = (EmptyInitNested){{uint8_t(seed), seed}, 0x5a};
            return uint64_t(nested.inner.low) | (uint64_t(nested.inner.high) << 8)
                | (uint64_t(nested.tag) << 24);
        case 16:
            return instruction.raw;
        case 17:
            return uint64_t(extended.low) | (uint64_t(extended.high) << 8)
                | (uint64_t(extended.tag) << 24);
        case 18:
            return named.raw;
        case 19:
            named = {.alternative = uint32_t(seed) | 0x56780000u};
            return named.alternative;
        case 20:
            extended = {{}, 0x55aa};
            return uint64_t(extended.low) | (uint64_t(extended.high) << 8)
                | (uint64_t(extended.tag) << 24);
        case 21:
            instruction = {};
            return instruction.raw;
        default:
            break;
        }
        return uint64_t(plain.low) | (uint64_t(plain.high) << 8);
    }
};
