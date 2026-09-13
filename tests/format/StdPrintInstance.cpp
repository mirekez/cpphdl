#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "../../tribe_cpu/common/Memory.h"

#include <filesystem>
#include <fstream>
#include <print>
#include <string>

using namespace cpphdl;

class StdPrintInstance : public Module
{
public:
    void _assign() {}
    void _work(bool) {}
    void _strobe() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)
long _system_clock = -1;

int main()
{
    const std::filesystem::path path = "generated/Memory.sv";
    std::ifstream input(path);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    const bool instance_format = text.find("$write(\"%m: port0:") != std::string::npos;
    const bool dangling_string = text.find("$write(\"%s: port0:") != std::string::npos;
    if (!instance_format || dangling_string) {
        std::print("std::print instance-name conversion regression in {}\n", path.string());
        return 1;
    }
    return 0;
}
#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
