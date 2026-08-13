#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>
#include <filesystem>
#include <fstream>
#include <print>

using namespace cpphdl;

class MemoryArray : public Module
{
private:
    // (* ram_style = "block" *)
    memory<u8, 4, 32> banks[3];
    memory<logic<64>, 1, 32> words
        [[clang::annotate("CPPHDL_ATTRIBUTE=(* ram_style = \"distributed\" *)")]];

public:
    void _work(bool reset)
    {
        size_t bank = 0;
        size_t address = 0;
        if (!reset) {
            banks[bank][address] = 0x12345678u;
        }
    }

    void _strobe()
    {
        for (size_t bank = 0; bank < 3; ++bank) banks[bank].apply();
    }

    void _assign() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)
long _system_clock = -1;

int main()
{
    std::ifstream in("generated/MemoryArray.sv");
    std::string text((std::istreambuf_iterator<char>(in)), {});
    const bool declaration_ok = text.find(
        "(* ram_style = \"block\" *)\n    reg[4-1:0][8-1:0] banks[3][32];")
        != std::string::npos;
    const bool singleton_ok = text.find(
        "(* ram_style = \"distributed\" *)\n    reg[64-1:0] words[32];")
        != std::string::npos
        && text.find("reg[1-1:0][64-1:0] words[32];") == std::string::npos;
    const bool access_ok = text.find("banks[bank][address] <=") != std::string::npos;
    if (!declaration_ok || !singleton_ok || !access_ok) {
        std::print("memory array dimension regression\n");
        return 1;
    }
    return 0;
}
#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
