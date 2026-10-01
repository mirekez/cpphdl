#include <cpphdl.h>
using namespace cpphdl;

class PortBindingChild : public Module {
public:
    _PORT(logic<32>) address_in;
    _PORT(logic<32>) constant_in;
    _PORT(logic<32>) same_width_in;
    _PORT(logic<64>) signed_in;
    _PORT(logic<64>) unsigned_in;
    _PORT(logic<13>) narrow_in;
    _PORT(bool) truth_in;
    _PORT(int64_t) scalar_in;
    _PORT(logic<8>) pointer_in;
    _PORT(logic<32>) address_out;
    _PORT(logic<32>) constant_out;
    _PORT(logic<32>) same_width_out;
    _PORT(logic<64>) sign_extended_out;
    _PORT(logic<64>) zero_extended_out;
    _PORT(logic<13>) narrow_out;
    _PORT(bool) truth_out;
    _PORT(logic<64>) scalar_out;
    _PORT(logic<8>) pointer_out;

    void _assign() {
        address_out = _ASSIGN(logic<32>(address_in().bits(31, 0)));
        constant_out = _ASSIGN(logic<32>(constant_in().bits(31, 0)));
        same_width_out = _ASSIGN(logic<32>(same_width_in().bits(31, 0)));
        sign_extended_out = _ASSIGN(signed_in());
        zero_extended_out = _ASSIGN(unsigned_in());
        narrow_out = _ASSIGN(narrow_in());
        truth_out = _ASSIGN(truth_in());
        scalar_out = _ASSIGN(logic<64>(scalar_in()));
        pointer_out = _ASSIGN(logic<8>(pointer_in().bits(7, 0)));
    }
};

class PortBinding : public Module {
public:
    _PORT(logic<64>) data_in;
    _PORT(int32_t) source_signed_in;
    _PORT(uint32_t) source_unsigned_in;
    _PORT(logic<8>) source_pointer_in;
    _PORT(logic<13>) direct_narrow_out;
    _PORT(bool) direct_truth_out;
    _PORT(logic<32>) address_out;
    _PORT(logic<32>) constant_out;
    _PORT(logic<32>) same_width_out;
    _PORT(logic<64>) sign_extended_out;
    _PORT(logic<64>) zero_extended_out;
    _PORT(logic<13>) narrow_out;
    _PORT(bool) truth_out;
    _PORT(logic<64>) scalar_out;
    _PORT(logic<8>) pointer_out;
    PortBindingChild child;

    void _assign() {
        // Top-level output evaluation uses read(port), not operator().
        direct_narrow_out = _ASSIGN(uint64_t(data_in()));
        direct_truth_out = _ASSIGN(uint64_t(data_in()));
#ifdef PORT_BINDING_TYPED
        child.address_in = _ASSIGN(logic<32>(uint64_t(data_in())));
        child.constant_in = _ASSIGN(logic<32>(0x10000));
#else
        child.address_in = _ASSIGN(uint64_t(data_in()));
        child.constant_in = _ASSIGN(uint64_t(0x10000));
#endif
        child.signed_in = _ASSIGN(source_signed_in());
        child.same_width_in = _ASSIGN(source_unsigned_in());
        child.unsigned_in = _ASSIGN(source_unsigned_in());
        child.narrow_in = _ASSIGN(uint64_t(data_in()));
        child.truth_in = _ASSIGN(uint64_t(data_in()));
        // An incompatible pointer binding copies and converts the pointee.
        child.scalar_in = _ASSIGN_COMB(source_signed_in());
        child.pointer_in = _ASSIGN_COMB(source_pointer_in());
        address_out = _ASSIGN(child.address_out());
        constant_out = _ASSIGN(child.constant_out());
        same_width_out = _ASSIGN(child.same_width_out());
        sign_extended_out = _ASSIGN(child.sign_extended_out());
        zero_extended_out = _ASSIGN(child.zero_extended_out());
        narrow_out = _ASSIGN(child.narrow_out());
        truth_out = _ASSIGN(child.truth_out());
        scalar_out = _ASSIGN(child.scalar_out());
        pointer_out = _ASSIGN_COMB(child.pointer_out());
        child._assign();
    }
};

extern PortBinding cpphdl_top;
