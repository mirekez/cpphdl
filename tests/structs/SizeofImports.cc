#include <cpphdl.h>
#include <cstdint>
using namespace cpphdl;

namespace SizeofTypes {
struct Word { uint32_t low; uint32_t high; };
struct Nested { Word first; Word second; };
struct ParentOnly { uint64_t words[3]; };
template<unsigned COUNT> struct Bytes { uint8_t data[COUNT]; };
using ThreeBytes = Bytes<3>;
}

// No variable or port has one of these struct types: sizeof alone must
// register the type dependency, including its concrete specialization name.
class SizeofTypeOnly : public Module {
public:
    _PORT(logic<16>) seed_in;
    _PORT(logic<64>) result_out = _ASSIGN_COMB(result_comb_func());
    logic<64> result_comb;
    logic<64>& result_comb_func() {
        result_comb = sizeof(SizeofTypes::Word)
            | (uint64_t(sizeof(const volatile SizeofTypes::Word)) << 8)
            | (uint64_t(sizeof(uint16_t)) << 16)
            | (uint64_t(seed_in()) << 32);
        return result_comb;
    }
};

class SizeofTemplateOnly : public Module {
public:
    _PORT(logic<64>) result_out = _ASSIGN_COMB(result_comb_func());
    logic<64> result_comb;
    logic<64>& result_comb_func() {
        result_comb = sizeof(SizeofTypes::Bytes<3>)
            | (uint64_t(sizeof(SizeofTypes::Bytes<5>)) << 8)
            | (uint64_t(sizeof(SizeofTypes::ThreeBytes)) << 16);
        return result_comb;
    }
};

class SizeofExprOnly : public Module {
public:
    _PORT(logic<64>) result_out = _ASSIGN_COMB(result_comb_func());
    logic<64> result_comb;
    logic<64>& result_comb_func() {
        // sizeof never evaluates its operand, including this null dereference.
        result_comb = sizeof(SizeofTypes::Word{})
            | (uint64_t(sizeof(*static_cast<const SizeofTypes::Word*>(nullptr))) << 8)
            | (uint64_t(sizeof(SizeofTypes::Nested{})) << 16);
        return result_comb;
    }
};

class SizeofImports : public Module {
public:
    static constexpr unsigned PARENT_BYTES = sizeof(SizeofTypes::ParentOnly);
    _PORT(logic<16>) seed_in;
    _PORT(logic<2>) mode_in;
    _PORT(logic<64>) result_out = _ASSIGN_COMB(result_comb_func());
    SizeofTypeOnly type_child;
    SizeofTemplateOnly template_child;
    SizeofExprOnly expr_child;
    logic<64> result_comb;

    void _assign() {
        type_child.seed_in = seed_in;
    }

    logic<64>& result_comb_func() {
        switch (unsigned(mode_in())) {
        case 0: result_comb = type_child.result_out(); break;
        case 1: result_comb = template_child.result_out(); break;
        case 2: result_comb = expr_child.result_out(); break;
        default:
            result_comb = (uint64_t(PARENT_BYTES) << 32)
                | (uint64_t(sizeof(SizeofTypes::ParentOnly)) + uint64_t(seed_in()));
            break;
        }
        return result_comb;
    }
};
