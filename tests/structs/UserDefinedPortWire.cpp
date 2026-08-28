#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

// Regression for ports whose SystemVerilog data type is declared in a package.
// With `default_nettype none`, these ports still need an explicit `wire` net kind.
struct UserPortStruct
{
    u8 low;
    u8 high;
} __PACKED;

union UserPortUnion
{
    UserPortStruct fields;
    logic<16> raw;
} __PACKED;

using UserPortBus = array<4, UserPortUnion, true>;

class UserDefinedPortWire : public Module
{
public:
    _PORT(UserPortStruct) struct_in;
    _PORT(UserPortStruct) struct_out;
    _PORT(UserPortUnion) union_in;
    _PORT(UserPortUnion) union_out;
    _PORT(UserPortBus) packed_union_in;
    _PORT(UserPortBus) packed_union_out;
    _PORT(UserPortStruct) unpacked_struct_in[2];
    _PORT(UserPortStruct) unpacked_struct_out[2];

    void _work(bool) {}
    void _strobe() {}
    void _assign() {}
};

/////////////////////////////////////////////////////////////////////////

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

long _system_clock = -1;

static bool require_text(const std::string& generated, const std::string& expected)
{
    if (generated.find(expected) != std::string::npos) {
        return true;
    }
    std::cerr << "ERROR: generated UserDefinedPortWire.sv does not contain: "
              << expected << '\n';
    return false;
}

int main()
{
    std::ifstream input("generated/UserDefinedPortWire.sv");
    if (!input) {
        std::cerr << "ERROR: cannot open generated/UserDefinedPortWire.sv\n";
        return 1;
    }
    const std::string generated{std::istreambuf_iterator<char>(input),
                                std::istreambuf_iterator<char>()};

    bool ok = true;
    ok &= require_text(generated, "input wire UserPortStruct struct_in");
    ok &= require_text(generated, "output wire UserPortStruct struct_out");
    ok &= require_text(generated, "input wire UserPortUnion union_in");
    ok &= require_text(generated, "output wire UserPortUnion union_out");
    // This is the shape used by SmartNIC RxFifo's array<8, RxDescriptorWord, true>.
    ok &= require_text(generated, "input wire UserPortUnion[4-1:0] packed_union_in");
    ok &= require_text(generated, "output wire UserPortUnion[4-1:0] packed_union_out");
    ok &= require_text(generated, "input wire UserPortStruct unpacked_struct_in[2]");
    ok &= require_text(generated, "output wire UserPortStruct unpacked_struct_out[2]");
    return ok ? 0 : 1;
}

#endif
